#include "lvgl_platform/device_profile.h"

#include "lvgl_platform/version.h"

#include <cstdlib>

#define RAPIDJSON_MALLOC(size) std::malloc(size)
#define RAPIDJSON_REALLOC(pointer, size) std::realloc(pointer, size)
#define RAPIDJSON_FREE(pointer) std::free(pointer)
#include <rapidjson/document.h>

#include <algorithm>
#include <initializer_list>

namespace lvgl_platform {
namespace {

struct StandardAllocator {
    static constexpr bool kNeedFree = true;

    void* Malloc(std::size_t size) { return size == 0 ? nullptr : std::malloc(size); }
    void* Realloc(void* pointer, std::size_t, std::size_t size)
    {
        if(size == 0) {
            std::free(pointer);
            return nullptr;
        }
        return std::realloc(pointer, size);
    }
    static void Free(void* pointer) noexcept { std::free(pointer); }
};

using Value = rapidjson::GenericValue<rapidjson::UTF8<>, StandardAllocator>;

bool has_only_members(const Value& object, std::initializer_list<std::string_view> allowed)
{
    if(!object.IsObject()) return false;
    for(auto member = object.MemberBegin(); member != object.MemberEnd(); ++member) {
        const std::string_view name(member->name.GetString(), member->name.GetStringLength());
        if(std::find(allowed.begin(), allowed.end(), name) == allowed.end()) return false;
    }
    return true;
}

bool read_string(const Value& object, const char* key, std::string& output)
{
    const auto member = object.FindMember(key);
    if(member == object.MemberEnd() || !member->value.IsString() ||
       member->value.GetStringLength() == 0 || member->value.GetStringLength() > 255) {
        return false;
    }
    output.assign(member->value.GetString(), member->value.GetStringLength());
    return output.find('\0') == std::string::npos;
}

bool read_positive_int(const Value& object, const char* key, std::int32_t& output)
{
    const auto member = object.FindMember(key);
    if(member == object.MemberEnd() || !member->value.IsInt()) return false;
    output = member->value.GetInt();
    return output > 0 && output <= 8192;
}

bool read_int(const Value& object, const char* key, std::int32_t& output)
{
    const auto member = object.FindMember(key);
    if(member == object.MemberEnd() || !member->value.IsInt()) return false;
    output = member->value.GetInt();
    return output >= -8192 && output <= 8192;
}

bool read_bool(const Value& object, const char* key, bool& output)
{
    const auto member = object.FindMember(key);
    if(member == object.MemberEnd() || !member->value.IsBool()) return false;
    output = member->value.GetBool();
    return true;
}

const Value* read_object(const Value& object, const char* key)
{
    const auto member = object.FindMember(key);
    if(member == object.MemberEnd() || !member->value.IsObject()) return nullptr;
    return &member->value;
}

bool read_size(const Value& object, Size& size)
{
    return read_positive_int(object, "width", size.width) &&
           read_positive_int(object, "height", size.height);
}

bool read_transform(const Value& object, Transform& transform)
{
    std::int32_t degrees = 0;
    if(!read_int(object, "clockwiseDegrees", degrees) ||
       (degrees != 0 && degrees != 90 && degrees != 180 && degrees != 270) ||
       !read_int(object, "offsetX", transform.offset_x) ||
       !read_int(object, "offsetY", transform.offset_y)) {
        return false;
    }
    transform.clockwise_degrees = static_cast<std::uint16_t>(degrees);
    return true;
}

std::int32_t clamp_axis(std::int32_t value, std::int32_t length) noexcept
{
    return std::clamp(value, std::int32_t {0}, length - 1);
}

Point rotate_logical(Point point, const Size& logical, const Transform& transform) noexcept
{
    point.x = clamp_axis(point.x, logical.width);
    point.y = clamp_axis(point.y, logical.height);
    switch(transform.clockwise_degrees) {
        case 0:
            return {transform.offset_x + point.x, transform.offset_y + point.y};
        case 90:
            return {transform.offset_x + logical.height - 1 - point.y,
                    transform.offset_y + point.x};
        case 180:
            return {transform.offset_x + logical.width - 1 - point.x,
                    transform.offset_y + logical.height - 1 - point.y};
        case 270:
            return {transform.offset_x + point.y,
                    transform.offset_y + logical.width - 1 - point.x};
    }
    return {};
}

Point inverse_rotate(Point point, const Size& logical, const Transform& transform) noexcept
{
    const auto x = point.x - transform.offset_x;
    const auto y = point.y - transform.offset_y;
    Point logical_point;
    switch(transform.clockwise_degrees) {
        case 0: logical_point = {x, y}; break;
        case 90: logical_point = {y, logical.height - 1 - x}; break;
        case 180:
            logical_point = {logical.width - 1 - x, logical.height - 1 - y};
            break;
        case 270: logical_point = {logical.width - 1 - y, x}; break;
        default: return {};
    }
    logical_point.x = clamp_axis(logical_point.x, logical.width);
    logical_point.y = clamp_axis(logical_point.y, logical.height);
    return logical_point;
}

}  // namespace

ProfileParseResult parse_device_profile(std::string_view json)
{
    ProfileParseResult result;
    result.error = errors::profile_invalid;
    if(json.empty() || json.size() > 64U * 1024U) {
        result.detail = "profile length is outside the allowed range";
        return result;
    }

    rapidjson::GenericDocument<rapidjson::UTF8<>, StandardAllocator, StandardAllocator> document;
    document.Parse(json.data(), json.size());
    if(document.HasParseError() || !document.IsObject()) {
        result.detail = "profile is not valid JSON object data";
        return result;
    }

    const auto schema = document.FindMember("schemaVersion");
    if(schema == document.MemberEnd() || !schema->value.IsUint() ||
       schema->value.GetUint() != kProfileSchemaVersion) {
        result.error = errors::profile_unsupported_schema;
        result.detail = "unsupported profile schema version";
        return result;
    }

    const auto* match = read_object(document, "match");
    const auto* logical = read_object(document, "logicalScreen");
    const auto* physical = read_object(document, "physicalScreen");
    const auto* display = read_object(document, "displayTransform");
    const auto* touch = read_object(document, "touchTransform");
    const auto* devices = read_object(document, "devices");
    const auto* requirements = read_object(document, "requirements");
    const auto* certification = read_object(document, "certification");
    if(match == nullptr || logical == nullptr || physical == nullptr || display == nullptr ||
       touch == nullptr || devices == nullptr || requirements == nullptr ||
       certification == nullptr ||
       !has_only_members(document,
                         {"schemaVersion", "profileId", "match", "logicalScreen",
                          "physicalScreen", "displayTransform", "touchTransform", "devices",
                          "requirements", "certification"}) ||
       !has_only_members(*match, {"model", "firmware", "pcba", "machine", "libc", "bits"}) ||
       !has_only_members(*logical, {"width", "height"}) ||
       !has_only_members(*physical, {"width", "height"}) ||
       !has_only_members(*display, {"clockwiseDegrees", "offsetX", "offsetY"}) ||
       !has_only_members(*touch, {"clockwiseDegrees", "offsetX", "offsetY"}) ||
       !has_only_members(*devices, {"drm", "connector", "touchName", "pixelFormat"}) ||
       !has_only_members(*requirements, {"overlayPlane", "falconTouchForwarding"}) ||
       !read_string(document, "profileId", result.profile.id) ||
       !read_string(*match, "model", result.profile.match.model) ||
       !read_string(*match, "firmware", result.profile.match.firmware) ||
       !read_string(*match, "pcba", result.profile.match.pcba) ||
       !read_string(*match, "machine", result.profile.match.machine) ||
       !read_string(*match, "libc", result.profile.match.libc) ||
       !read_size(*logical, result.profile.logical) ||
       !read_size(*physical, result.profile.physical) ||
       !read_transform(*display, result.profile.display) ||
       !read_transform(*touch, result.profile.touch) ||
       !read_string(*devices, "drm", result.profile.drm_device) ||
       !read_string(*devices, "connector", result.profile.connector) ||
       !read_string(*devices, "touchName", result.profile.touch_name) ||
       !read_string(*devices, "pixelFormat", result.profile.pixel_format) ||
       !read_bool(*requirements, "overlayPlane", result.profile.requires_overlay_plane) ||
       !read_bool(*requirements, "falconTouchForwarding",
                  result.profile.requires_touch_forwarding)) {
        result.detail = "profile is missing a required field or contains an invalid value";
        return result;
    }

    const auto bits = match->FindMember("bits");
    if(bits == match->MemberEnd() || !bits->value.IsUint() ||
       (bits->value.GetUint() != 32 && bits->value.GetUint() != 64)) {
        result.detail = "profile bits must be 32 or 64";
        return result;
    }
    result.profile.match.bits = static_cast<std::uint16_t>(bits->value.GetUint());

    const Point physical_corners[] = {
        logical_to_physical({0, 0}, result.profile),
        logical_to_physical({result.profile.logical.width - 1, 0}, result.profile),
        logical_to_physical({0, result.profile.logical.height - 1}, result.profile),
        logical_to_physical(
            {result.profile.logical.width - 1, result.profile.logical.height - 1},
            result.profile),
    };
    for(const auto& corner : physical_corners) {
        if(corner.x < 0 || corner.y < 0 || corner.x >= result.profile.physical.width ||
           corner.y >= result.profile.physical.height) {
            result.detail = "display transform falls outside the physical screen";
            return result;
        }
    }

    result.error = errors::ok;
    result.detail.clear();
    return result;
}

bool profile_matches(const DeviceProfile& profile, const DeviceIdentity& identity) noexcept
{
    return profile.match.model == identity.model &&
           profile.match.firmware == identity.firmware && profile.match.pcba == identity.pcba &&
           profile.match.machine == identity.machine && profile.match.libc == identity.libc &&
           profile.match.bits == identity.bits;
}

ProfileSelection select_device_profile(
    const std::vector<DeviceProfile>& profiles, const DeviceIdentity& identity) noexcept
{
    ProfileSelection selection;
    for(const auto& profile : profiles) {
        if(!profile_matches(profile, identity)) continue;
        if(selection.profile != nullptr) {
            selection.profile = nullptr;
            selection.error = errors::profile_ambiguous;
            return selection;
        }
        selection.profile = &profile;
        selection.error = errors::ok;
    }
    return selection;
}

Point logical_to_physical(Point logical, const DeviceProfile& profile) noexcept
{
    return rotate_logical(logical, profile.logical, profile.display);
}

Point physical_touch_to_logical(Point physical, const DeviceProfile& profile) noexcept
{
    return inverse_rotate(physical, profile.logical, profile.touch);
}

}  // namespace lvgl_platform
