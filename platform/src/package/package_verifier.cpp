#include "lvgl_platform/package_verifier.h"

#include <cstdlib>

#define RAPIDJSON_MALLOC(size) std::malloc(size)
#define RAPIDJSON_REALLOC(pointer, size) std::realloc(pointer, size)
#define RAPIDJSON_FREE(pointer) std::free(pointer)
#include <rapidjson/document.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <string_view>

namespace lvgl_platform {
namespace {

constexpr std::size_t kHeaderSize = 64;
constexpr std::size_t kSignatureSize = 96;
constexpr std::size_t kMaximumPackageSize = 256U * 1024U * 1024U;
constexpr std::size_t kMaximumManifestSize = 64U * 1024U;
constexpr std::size_t kMaximumFiles = 256;
constexpr std::size_t kMaximumPathSize = 240;
constexpr std::uint32_t kDevelopmentFlag = 1;
constexpr std::array<std::uint8_t, 8> kPackageMagic {'L', 'V', 'A', 'P', 'P', '0', '0', '1'};
constexpr std::array<std::uint8_t, 8> kSignatureMagic {'L', 'V', 'S', 'I', 'G', '0', '0', '1'};
constexpr std::array<std::uint8_t, 16> kSignatureDomain {
    'L', 'V', 'A', 'P', 'P', '-', 'S', 'I', 'G', 'N', '-', 'V', '1', 0, 0, 0};
constexpr std::array<std::uint8_t, 12> kEd25519SpkiPrefix {
    0x30, 0x2a, 0x30, 0x05, 0x06, 0x03, 0x2b, 0x65, 0x70, 0x03, 0x21, 0x00};

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

using JsonValue = rapidjson::GenericValue<rapidjson::UTF8<>, StandardAllocator>;
using JsonDocument =
    rapidjson::GenericDocument<rapidjson::UTF8<>, StandardAllocator, StandardAllocator>;

PackageVerification failure(PackageStatus status, std::string detail)
{
    PackageVerification result;
    result.status = status;
    result.detail = std::move(detail);
    return result;
}

std::uint16_t read_u16(const std::uint8_t* bytes) noexcept
{
    return static_cast<std::uint16_t>(bytes[0]) |
           static_cast<std::uint16_t>(bytes[1] << 8U);
}

std::uint32_t read_u32(const std::uint8_t* bytes) noexcept
{
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

std::uint64_t read_u64(const std::uint8_t* bytes) noexcept
{
    std::uint64_t value = 0;
    for(std::size_t index = 0; index < 8; ++index) {
        value |= static_cast<std::uint64_t>(bytes[index]) << (index * 8U);
    }
    return value;
}

bool checked_add(std::uint64_t left, std::uint64_t right, std::uint64_t& output) noexcept
{
    if(right > std::numeric_limits<std::uint64_t>::max() - left) return false;
    output = left + right;
    return true;
}

template <typename Container>
bool bytes_equal(const std::uint8_t* bytes, const Container& expected) noexcept
{
    return std::equal(expected.begin(), expected.end(), bytes);
}

bool has_exact_members(const JsonValue& value, std::initializer_list<std::string_view> names)
{
    if(!value.IsObject() || value.MemberCount() != names.size()) return false;
    for(const auto name : names) {
        if(value.FindMember(
               rapidjson::GenericStringRef<char>(name.data(),
                                                  static_cast<rapidjson::SizeType>(name.size()))) ==
           value.MemberEnd()) {
            return false;
        }
    }
    return true;
}

const JsonValue* member(const JsonValue& value, const char* name)
{
    const auto found = value.FindMember(name);
    return found == value.MemberEnd() ? nullptr : &found->value;
}

bool string_value(
    const JsonValue* value, std::string& output, std::size_t maximum = 255,
    bool allow_empty = false)
{
    if(value == nullptr || !value->IsString() || value->GetStringLength() > maximum ||
       (!allow_empty && value->GetStringLength() == 0)) {
        return false;
    }
    output.assign(value->GetString(), value->GetStringLength());
    return output.find('\0') == std::string::npos;
}

bool canonical_string(std::string_view value, std::string& output)
{
    constexpr char hex[] = "0123456789abcdef";
    output.push_back('"');
    for(const auto raw : value) {
        const auto byte = static_cast<unsigned char>(raw);
        switch(byte) {
            case '"': output += "\\\""; break;
            case '\\': output += "\\\\"; break;
            case '\b': output += "\\b"; break;
            case '\f': output += "\\f"; break;
            case '\n': output += "\\n"; break;
            case '\r': output += "\\r"; break;
            case '\t': output += "\\t"; break;
            default:
                if(byte < 0x20) {
                    output += "\\u00";
                    output.push_back(hex[byte >> 4U]);
                    output.push_back(hex[byte & 0x0fU]);
                } else {
                    output.push_back(static_cast<char>(byte));
                }
        }
    }
    output.push_back('"');
    return true;
}

bool canonical_json(const JsonValue& value, std::string& output)
{
    if(value.IsNull()) {
        output += "null";
        return true;
    }
    if(value.IsBool()) {
        output += value.GetBool() ? "true" : "false";
        return true;
    }
    if(value.IsString()) {
        return canonical_string(
            std::string_view(value.GetString(), value.GetStringLength()), output);
    }
    if(value.IsInt64()) {
        const auto number = value.GetInt64();
        if(number < -9007199254740991LL || number > 9007199254740991LL) return false;
        output += std::to_string(number);
        return true;
    }
    if(value.IsUint64()) {
        if(value.GetUint64() > 9007199254740991ULL) return false;
        output += std::to_string(value.GetUint64());
        return true;
    }
    if(value.IsArray()) {
        output.push_back('[');
        for(rapidjson::SizeType index = 0; index < value.Size(); ++index) {
            if(index != 0) output.push_back(',');
            if(!canonical_json(value[index], output)) return false;
        }
        output.push_back(']');
        return true;
    }
    if(value.IsObject()) {
        output.push_back('{');
        std::string_view previous;
        bool first = true;
        for(auto item = value.MemberBegin(); item != value.MemberEnd(); ++item) {
            const std::string_view name(item->name.GetString(), item->name.GetStringLength());
            if(!first && previous >= name) return false;
            if(!first) output.push_back(',');
            canonical_string(name, output);
            output.push_back(':');
            if(!canonical_json(item->value, output)) return false;
            previous = name;
            first = false;
        }
        output.push_back('}');
        return true;
    }
    return false;
}

bool semantic_version(std::string_view value) noexcept
{
    if(value.empty() || value.size() > 32) return false;
    int dots = 0;
    bool digit = false;
    for(const auto character : value) {
        if(character == '.') {
            if(!digit) return false;
            ++dots;
            digit = false;
        } else if(character >= '0' && character <= '9') {
            digit = true;
        } else {
            return false;
        }
    }
    return dots == 2 && digit;
}

bool app_id(std::string_view value) noexcept
{
    if(value.empty() || value.size() > 96 || value.front() == '.' || value.back() == '.') {
        return false;
    }
    bool separator = false;
    bool has_separator = false;
    for(const auto character : value) {
        const bool valid = (character >= 'a' && character <= 'z') ||
                           (character >= '0' && character <= '9');
        if(character == '.' || character == '-') {
            if(separator) return false;
            separator = true;
            has_separator = true;
        } else if(valid) {
            separator = false;
        } else {
            return false;
        }
    }
    return !separator && has_separator;
}

bool valid_path(std::string_view path) noexcept
{
    if(path.empty() || path.size() > kMaximumPathSize || path.front() == '/' ||
       path.back() == '/' || path.find('\\') != std::string_view::npos ||
       path.find("//") != std::string_view::npos || path.find('\0') != std::string_view::npos) {
        return false;
    }
    std::size_t start = 0;
    while(start < path.size()) {
        const auto end = path.find('/', start);
        const auto segment = path.substr(start, end == std::string_view::npos ? path.size() - start
                                                                              : end - start);
        if(segment.empty() || segment == "." || segment == ".." || segment.back() == '.') return false;
        for(const auto raw : segment) {
            const auto byte = static_cast<unsigned char>(raw);
            if(byte < 0x20 || byte == 0x7f || std::strchr(":*?\"<>|", byte) != nullptr) return false;
        }
        if(end == std::string_view::npos) break;
        start = end + 1;
    }
    return true;
}

bool lowercase_hex(std::string_view value, std::size_t size) noexcept
{
    if(value.size() != size) return false;
    return std::all_of(value.begin(), value.end(), [](char character) {
        return (character >= '0' && character <= '9') ||
               (character >= 'a' && character <= 'f');
    });
}

bool decode_hex_32(std::string_view value, Sha256Digest& output) noexcept
{
    if(!lowercase_hex(value, 64)) return false;
    auto nibble = [](char character) {
        return static_cast<std::uint8_t>(character <= '9' ? character - '0'
                                                          : character - 'a' + 10);
    };
    for(std::size_t index = 0; index < output.size(); ++index) {
        output[index] = static_cast<std::uint8_t>(
            (nibble(value[index * 2]) << 4U) | nibble(value[index * 2 + 1]));
    }
    return true;
}

bool sorted_string_array(
    const JsonValue* value, std::vector<std::string>& output, bool allow_empty = false)
{
    if(value == nullptr || !value->IsArray() || (!allow_empty && value->Empty()) ||
       value->Size() > 64) {
        return false;
    }
    std::string previous;
    for(const auto& item : value->GetArray()) {
        std::string current;
        if(!string_value(&item, current) || (!previous.empty() && previous >= current)) return false;
        output.push_back(current);
        previous = std::move(current);
    }
    return true;
}

bool parse_manifest(const JsonDocument& document, VerifiedPackageManifest& manifest)
{
    if(!has_exact_members(
           document,
           {"appId", "capabilities", "entry", "files", "formatVersion", "limits",
            "minPlatformVersion", "name", "onlinePolicy", "releaseCounter", "sdkAbi",
            "securityEpoch", "signingKeyId", "supportedMachines", "supportedProfiles",
            "version"})) {
        return false;
    }
    const auto* format = member(document, "formatVersion");
    const auto* counter = member(document, "releaseCounter");
    const auto* epoch = member(document, "securityEpoch");
    if(format == nullptr || !format->IsUint() || format->GetUint() != 1 || counter == nullptr ||
       !counter->IsUint64() || counter->GetUint64() == 0 || counter->GetUint64() > 9007199254740991ULL ||
       epoch == nullptr || !epoch->IsUint()) {
        return false;
    }
    manifest.release_counter = counter->GetUint64();
    manifest.security_epoch = epoch->GetUint();
    if(!string_value(member(document, "appId"), manifest.app_id, 96) ||
       !string_value(member(document, "name"), manifest.name, 64) ||
       !string_value(member(document, "version"), manifest.version, 32) ||
       !string_value(member(document, "entry"), manifest.entry, kMaximumPathSize) ||
       !string_value(member(document, "sdkAbi"), manifest.sdk_abi, 16) ||
       !string_value(member(document, "minPlatformVersion"), manifest.minimum_platform_version, 32) ||
       !string_value(member(document, "signingKeyId"), manifest.signing_key_id, 32) ||
       !app_id(manifest.app_id) || !semantic_version(manifest.version) ||
       !semantic_version(manifest.minimum_platform_version) || manifest.sdk_abi != "1.0" ||
       !valid_path(manifest.entry) || !lowercase_hex(manifest.signing_key_id, 32) ||
       !sorted_string_array(member(document, "supportedProfiles"), manifest.supported_profiles) ||
       !sorted_string_array(member(document, "supportedMachines"), manifest.supported_machines) ||
       !sorted_string_array(member(document, "capabilities"), manifest.capabilities, true)) {
        return false;
    }
    constexpr std::string_view allowed_capabilities[] = {
        "audio.output", "dictionary.lookup", "haptics", "microphone", "network", "scanner",
        "storage.private"};
    for(const auto& capability : manifest.capabilities) {
        if(std::find(std::begin(allowed_capabilities), std::end(allowed_capabilities), capability) ==
           std::end(allowed_capabilities)) {
            return false;
        }
    }

    const auto* limits = member(document, "limits");
    const auto* online = member(document, "onlinePolicy");
    if(limits == nullptr || !has_exact_members(
                              *limits, {"cpuSeconds", "dataMiB", "maxFiles", "memoryMiB"}) ||
       online == nullptr || !has_exact_members(*online, {"mode"})) {
        return false;
    }
    const auto* memory = member(*limits, "memoryMiB");
    const auto* cpu = member(*limits, "cpuSeconds");
    const auto* max_files = member(*limits, "maxFiles");
    const auto* data = member(*limits, "dataMiB");
    std::string online_mode;
    if(memory == nullptr || !memory->IsUint() || memory->GetUint() < 8 || memory->GetUint() > 512 ||
       cpu == nullptr || !cpu->IsUint() || cpu->GetUint() < 1 || cpu->GetUint() > 86400 ||
       max_files == nullptr || !max_files->IsUint() || max_files->GetUint() < 1 ||
       max_files->GetUint() > kMaximumFiles || data == nullptr || !data->IsUint() ||
       data->GetUint() < 1 || data->GetUint() > 4096 ||
       !string_value(member(*online, "mode"), online_mode, 32) || online_mode != "offline-v1") {
        return false;
    }

    const auto* files = member(document, "files");
    if(files == nullptr || !files->IsArray() || files->Empty() || files->Size() > kMaximumFiles) {
        return false;
    }
    std::string previous;
    bool entry_found = false;
    for(const auto& item : files->GetArray()) {
        if(!has_exact_members(item, {"mode", "path", "role", "sha256", "size"})) return false;
        VerifiedPackageFile file;
        std::string hash;
        const auto* mode = member(item, "mode");
        const auto* size = member(item, "size");
        if(mode == nullptr || !mode->IsUint() || (mode->GetUint() != 0644 && mode->GetUint() != 0755) ||
           size == nullptr || !size->IsUint64() || size->GetUint64() > kMaximumPackageSize ||
           !string_value(member(item, "path"), file.path, kMaximumPathSize) ||
           !string_value(member(item, "role"), file.role, 16) ||
           !string_value(member(item, "sha256"), hash, 64) || !valid_path(file.path) ||
           (!previous.empty() && previous >= file.path) ||
           (file.role != "asset" && file.role != "executable") ||
           !decode_hex_32(hash, file.sha256)) {
            return false;
        }
        file.mode = static_cast<std::uint16_t>(mode->GetUint());
        file.size = size->GetUint64();
        if(file.path == manifest.entry) {
            entry_found = file.role == "executable" && file.mode == 0755;
        }
        previous = file.path;
        manifest.files.push_back(std::move(file));
    }
    return entry_found;
}

std::array<std::uint8_t, 16> key_id(
    const CryptoProvider& crypto, const Ed25519PublicKey& key, bool& ok) noexcept
{
    std::array<std::uint8_t, 44> spki {};
    std::copy(kEd25519SpkiPrefix.begin(), kEd25519SpkiPrefix.end(), spki.begin());
    std::copy(key.begin(), key.end(), spki.begin() + kEd25519SpkiPrefix.size());
    Sha256Digest digest {};
    ok = crypto.sha256(spki.data(), spki.size(), digest);
    std::array<std::uint8_t, 16> id {};
    if(ok) std::copy_n(digest.begin(), id.size(), id.begin());
    return id;
}

}  // namespace

PackageVerification verify_package(
    const std::uint8_t* bytes, std::size_t size, const CryptoProvider& crypto,
    const std::vector<TrustedPublicKey>& trusted_keys, bool allow_development)
{
    if(bytes == nullptr || size < kHeaderSize || size > kMaximumPackageSize + kSignatureSize ||
       !bytes_equal(bytes, kPackageMagic) || read_u16(bytes + 8) != 1 ||
       read_u16(bytes + 10) != kHeaderSize) {
        return failure(PackageStatus::invalid_header, "PACKAGE_HEADER_INVALID");
    }
    const auto flags = read_u32(bytes + 12);
    if(flags != 0 && flags != kDevelopmentFlag) {
        return failure(PackageStatus::invalid_header, "PACKAGE_FLAGS_INVALID");
    }
    const bool development = flags == kDevelopmentFlag;
    if(development && !allow_development) {
        auto result = failure(PackageStatus::development_rejected, "PACKAGE_DEVELOPMENT_REJECTED");
        result.development = true;
        return result;
    }

    const auto manifest_size = read_u32(bytes + 16);
    const auto entry_count = read_u32(bytes + 20);
    const auto table_size = read_u64(bytes + 24);
    const auto payload_size = read_u64(bytes + 32);
    const auto unsigned_size = read_u64(bytes + 40);
    const auto total_size = read_u64(bytes + 48);
    std::uint64_t expected_unsigned = kHeaderSize;
    const bool layout_ok = manifest_size > 0 && manifest_size <= kMaximumManifestSize &&
                           entry_count > 0 && entry_count <= kMaximumFiles &&
                           read_u32(bytes + 56) == 1 && read_u32(bytes + 60) == 0 &&
                           checked_add(expected_unsigned, manifest_size, expected_unsigned) &&
                           checked_add(expected_unsigned, table_size, expected_unsigned) &&
                           checked_add(expected_unsigned, payload_size, expected_unsigned) &&
                           expected_unsigned == unsigned_size && total_size == size &&
                           total_size == unsigned_size + (development ? 0 : kSignatureSize);
    if(!layout_ok) return failure(PackageStatus::invalid_layout, "PACKAGE_LAYOUT_INVALID");

    JsonDocument document;
    document.Parse<rapidjson::kParseValidateEncodingFlag>(
        reinterpret_cast<const char*>(bytes + kHeaderSize), manifest_size);
    std::string canonical;
    if(document.HasParseError() || !document.IsObject() || !canonical_json(document, canonical) ||
       canonical.size() != manifest_size ||
       std::memcmp(canonical.data(), bytes + kHeaderSize, manifest_size) != 0) {
        return failure(PackageStatus::invalid_manifest, "PACKAGE_MANIFEST_NON_CANONICAL");
    }

    PackageVerification result;
    result.development = development;
    result.unsigned_size = unsigned_size;
    if(!parse_manifest(document, result.manifest)) {
        return failure(PackageStatus::invalid_manifest, "PACKAGE_MANIFEST_INVALID");
    }
    if(result.manifest.files.size() != entry_count) {
        return failure(PackageStatus::invalid_manifest, "PACKAGE_MANIFEST_FILE_COUNT_MISMATCH");
    }
    if(development && result.manifest.signing_key_id != std::string(32, '0')) {
        return failure(PackageStatus::invalid_manifest, "PACKAGE_DEVELOPMENT_KEY_ID_INVALID");
    }
    if(!development && result.manifest.signing_key_id == std::string(32, '0')) {
        return failure(PackageStatus::invalid_manifest, "PACKAGE_SIGNING_KEY_ID_INVALID");
    }

    const std::uint64_t table_start = kHeaderSize + manifest_size;
    const std::uint64_t payload_start = table_start + table_size;
    std::uint64_t cursor = table_start;
    std::uint64_t expected_offset = 0;
    std::string previous;
    for(std::size_t index = 0; index < entry_count; ++index) {
        if(cursor > payload_start || payload_start - cursor < 56) {
            return failure(PackageStatus::invalid_entry, "PACKAGE_TABLE_TRUNCATED");
        }
        const auto* record = bytes + cursor;
        const auto path_size = read_u16(record);
        const auto mode = read_u16(record + 2);
        const auto entry_flags = read_u32(record + 4);
        const auto file_size = read_u64(record + 8);
        const auto offset = read_u64(record + 16);
        cursor += 56;
        if(path_size == 0 || path_size > kMaximumPathSize || cursor > payload_start ||
           path_size > payload_start - cursor || entry_flags != 0 ||
           (mode != 0644 && mode != 0755) || offset != expected_offset || offset > payload_size ||
           file_size > payload_size - offset) {
            return failure(PackageStatus::invalid_entry, "PACKAGE_ENTRY_INVALID");
        }
        const std::string path_value(
            reinterpret_cast<const char*>(bytes + cursor), path_size);
        if(!valid_path(path_value)) return failure(PackageStatus::invalid_path, "PACKAGE_PATH_INVALID");
        if(!previous.empty() && previous >= path_value) {
            return failure(PackageStatus::invalid_entry, "PACKAGE_ENTRY_ORDER_INVALID");
        }
        const auto& manifest_file = result.manifest.files[index];
        if(manifest_file.path != path_value || manifest_file.mode != mode ||
           manifest_file.size != file_size ||
           !std::equal(manifest_file.sha256.begin(), manifest_file.sha256.end(), record + 24)) {
            return failure(PackageStatus::invalid_entry, "PACKAGE_MANIFEST_TABLE_MISMATCH");
        }
        Sha256Digest actual {};
        if(!crypto.sha256(bytes + payload_start + offset, file_size, actual)) {
            return failure(PackageStatus::crypto_unavailable, "PACKAGE_CRYPTO_UNAVAILABLE");
        }
        if(actual != manifest_file.sha256) {
            return failure(PackageStatus::content_hash_mismatch, "PACKAGE_CONTENT_HASH_MISMATCH");
        }
        result.manifest.files[index].payload_offset = offset;
        cursor += path_size;
        expected_offset += file_size;
        previous = path_value;
    }
    if(cursor != payload_start || expected_offset != payload_size) {
        return failure(PackageStatus::invalid_entry, "PACKAGE_TABLE_SIZE_MISMATCH");
    }

    if(development) {
        result.status = PackageStatus::verified;
        result.detail = "PACKAGE_DEVELOPMENT_STRUCTURALLY_VERIFIED";
        return result;
    }

    const auto* envelope = bytes + unsigned_size;
    if(!bytes_equal(envelope, kSignatureMagic) || read_u16(envelope + 8) != 1 ||
       read_u16(envelope + 10) != 1 || read_u32(envelope + 92) != 0) {
        return failure(PackageStatus::signature_invalid, "PACKAGE_SIGNATURE_ENVELOPE_INVALID");
    }
    const std::array<std::uint8_t, 16> envelope_key_id = [&] {
        std::array<std::uint8_t, 16> id {};
        std::copy_n(envelope + 12, id.size(), id.begin());
        return id;
    }();
    const auto id_hex = [&] {
        constexpr char hex[] = "0123456789abcdef";
        std::string output;
        output.reserve(32);
        for(const auto value : envelope_key_id) {
            output.push_back(hex[value >> 4U]);
            output.push_back(hex[value & 0x0fU]);
        }
        return output;
    }();
    if(id_hex != result.manifest.signing_key_id) {
        return failure(PackageStatus::signature_invalid, "PACKAGE_SIGNATURE_KEY_ID_MISMATCH");
    }

    const TrustedPublicKey* signer = nullptr;
    for(const auto& candidate : trusted_keys) {
        bool hash_ok = false;
        if(key_id(crypto, candidate.key, hash_ok) == envelope_key_id) {
            if(!hash_ok) return failure(PackageStatus::crypto_unavailable, "PACKAGE_CRYPTO_UNAVAILABLE");
            signer = &candidate;
            break;
        }
        if(!hash_ok) return failure(PackageStatus::crypto_unavailable, "PACKAGE_CRYPTO_UNAVAILABLE");
    }
    if(signer == nullptr || !signer->production) {
        return failure(PackageStatus::signer_untrusted, "PACKAGE_SIGNER_UNTRUSTED");
    }
    Sha512Digest unsigned_digest {};
    if(!crypto.sha512(bytes, unsigned_size, unsigned_digest)) {
        return failure(PackageStatus::crypto_unavailable, "PACKAGE_CRYPTO_UNAVAILABLE");
    }
    std::array<std::uint8_t, 80> message {};
    std::copy(kSignatureDomain.begin(), kSignatureDomain.end(), message.begin());
    std::copy(unsigned_digest.begin(), unsigned_digest.end(),
              message.begin() + kSignatureDomain.size());
    Ed25519Signature signature {};
    std::copy_n(envelope + 28, signature.size(), signature.begin());
    if(!crypto.verify_ed25519(signer->key, message.data(), message.size(), signature)) {
        return failure(PackageStatus::signature_invalid, "PACKAGE_SIGNATURE_INVALID");
    }
    result.status = PackageStatus::verified;
    result.detail = "PACKAGE_VERIFIED";
    result.signature_verified = true;
    return result;
}

}  // namespace lvgl_platform
