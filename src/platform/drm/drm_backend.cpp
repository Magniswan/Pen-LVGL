#include "platform/drm/drm_backend.h"

#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>

namespace dictpen {
namespace {

struct Buffer {
    uint32_t handle {};
    uint32_t pitch {};
    uint64_t size {};
    uint32_t framebuffer {};
    uint8_t* map {nullptr};
};

constexpr uint32_t kPageFlipFlags = DRM_MODE_PAGE_FLIP_EVENT;
constexpr unsigned int kMaxPageFlipRecoveries = 1;

const char* connector_type_name(uint32_t type)
{
    switch(type) {
        case DRM_MODE_CONNECTOR_DSI: return "DSI";
        case DRM_MODE_CONNECTOR_DPI: return "DPI";
        case DRM_MODE_CONNECTOR_LVDS: return "LVDS";
        case DRM_MODE_CONNECTOR_eDP: return "eDP";
        default: return "unknown";
    }
}

void page_flip_handler(int, unsigned int, unsigned int, unsigned int, void* data)
{
    auto* pending = static_cast<bool*>(data);
    *pending = false;
}

}  // namespace

struct DrmBackend::Impl {
    explicit Impl(const DeviceProfile& value) : profile(value) {}

    DeviceProfile profile;
    int fd {-1};
    uint32_t connector_id {};
    uint32_t crtc_id {};
    uint32_t plane_id {};
    uint32_t framebuffer_format {DRM_FORMAT_XRGB8888};
    drmModeModeInfo mode {};
    drmModeCrtcPtr saved_crtc {nullptr};
    std::vector<Buffer> buffers;
    int active_buffer {-1};
    bool flip_pending {false};
    bool modeset_done {false};
    bool overlay_mode {false};
    int32_t overlay_x {0};
    int32_t overlay_y {0};
    int32_t overlay_width {0};
    int32_t overlay_height {0};
    int32_t overlay_zpos {0};
    int32_t buffer_width {0};
    int32_t buffer_height {0};
    int32_t rotation {0};
    unsigned int page_flip_recoveries {};
    std::string error;

    bool fail(const char* operation)
    {
        error = std::string(operation) + ": " + std::strerror(errno);
        return false;
    }

    bool environment_integer(const char* name, int32_t minimum, int32_t maximum, int32_t& output)
    {
        const char* value = std::getenv(name);
        if(value == nullptr || *value == '\0') return false;
        char* end = nullptr;
        errno = 0;
        const long parsed = std::strtol(value, &end, 10);
        if(errno != 0 || end == value || *end != '\0' || parsed < minimum || parsed > maximum) {
            return false;
        }
        output = static_cast<int32_t>(parsed);
        return true;
    }

    bool configure_overlay()
    {
        if(std::getenv("LVGL_DRM_OVERLAY_PLANE_ID") == nullptr) return true;
        int32_t configured_plane = 0;
        int32_t configured_crtc = 0;
        int32_t configured_connector = 0;
        int32_t logical_width = 0;
        int32_t logical_height = 0;
        if(!environment_integer("LVGL_DRM_OVERLAY_PLANE_ID", 1,
                                std::numeric_limits<int32_t>::max(), configured_plane) ||
           !environment_integer("LVGL_DRM_CRTC_ID", 1,
                                std::numeric_limits<int32_t>::max(), configured_crtc) ||
           !environment_integer("LVGL_DRM_CONNECTOR_ID", 1,
                                std::numeric_limits<int32_t>::max(), configured_connector) ||
           !environment_integer("LVGL_DISPLAY_X", 0, 4096, overlay_x) ||
           !environment_integer("LVGL_DISPLAY_Y", 0, 4096, overlay_y) ||
           !environment_integer("LVGL_DISPLAY_WIDTH", 80, 4096, overlay_width) ||
           !environment_integer("LVGL_DISPLAY_HEIGHT", 80, 4096, overlay_height) ||
           !environment_integer("LVGL_DRM_OVERLAY_ZPOS", 0, 128, overlay_zpos) ||
           !environment_integer("LVGL_DISPLAY_ROTATION", 0, 270, rotation) ||
           !environment_integer("LVGL_LOGICAL_WIDTH", 240, 4096, logical_width) ||
           !environment_integer("LVGL_LOGICAL_HEIGHT", 80, 2048, logical_height) ||
           logical_width != profile.logical_width || logical_height != profile.logical_height ||
           (rotation != 0 && rotation != 90 && rotation != 180 && rotation != 270) ||
           overlay_x > 4096 - overlay_width || overlay_y > 4096 - overlay_height) {
            errno = EINVAL;
            return fail("certified overlay environment");
        }
        const char* device = std::getenv("LVGL_DRM_DEVICE");
        const char* pixel = std::getenv("LVGL_PIXEL_FORMAT");
        if(device == nullptr || std::strcmp(device, profile.drm_device) != 0 || pixel == nullptr ||
           (std::strcmp(pixel, "XRGB8888") != 0 && std::strcmp(pixel, "ARGB8888") != 0)) {
            errno = EINVAL;
            return fail("certified overlay format");
        }
        plane_id = static_cast<uint32_t>(configured_plane);
        crtc_id = static_cast<uint32_t>(configured_crtc);
        connector_id = static_cast<uint32_t>(configured_connector);
        framebuffer_format = std::strcmp(pixel, "ARGB8888") == 0
                                 ? DRM_FORMAT_ARGB8888
                                 : DRM_FORMAT_XRGB8888;
        buffer_width = rotation == 90 || rotation == 270 ? logical_height : logical_width;
        buffer_height = rotation == 90 || rotation == 270 ? logical_width : logical_height;
        mode.hdisplay = static_cast<uint16_t>(buffer_width);
        mode.vdisplay = static_cast<uint16_t>(buffer_height);
        overlay_mode = true;
        return true;
    }

    bool open_inherited()
    {
        const char* value = std::getenv("LVGL_DRM_FD");
        if(value == nullptr || *value == '\0') {
            errno = EBADF;
            return fail("inherited DRM descriptor missing");
        }
        char* end = nullptr;
        errno = 0;
        const long parsed = std::strtol(value, &end, 10);
        if(errno != 0 || end == value || *end != '\0' || parsed < 3 ||
           parsed > std::numeric_limits<int>::max()) {
            errno = EBADF;
            return fail("inherited DRM descriptor invalid");
        }
        struct stat inherited {};
        struct stat expected {};
        const int descriptor = static_cast<int>(parsed);
        if(::fstat(descriptor, &inherited) != 0 || ::stat(profile.drm_device, &expected) != 0 ||
           !S_ISCHR(inherited.st_mode) || !S_ISCHR(expected.st_mode) || inherited.st_uid != 0 ||
           inherited.st_rdev != expected.st_rdev) {
            errno = EBADF;
            return fail("inherited DRM descriptor identity");
        }
        const int descriptor_flags = ::fcntl(descriptor, F_GETFD);
        if(descriptor_flags < 0 ||
           ::fcntl(descriptor, F_SETFD, descriptor_flags | FD_CLOEXEC) != 0) {
            return fail("inherited DRM descriptor flags");
        }
        fd = descriptor;
        return true;
    }

    bool validate_overlay_target()
    {
        if(drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1) != 0) {
            return fail("DRM universal planes");
        }
        drmModeResPtr resources = drmModeGetResources(fd);
        if(resources == nullptr) return fail("drmModeGetResources overlay");
        int crtc_index = -1;
        for(int index = 0; index < resources->count_crtcs; ++index) {
            if(resources->crtcs[index] == crtc_id) crtc_index = index;
        }
        drmModeConnectorPtr connector = drmModeGetConnector(fd, connector_id);
        const bool connector_ok = connector != nullptr &&
                                  connector->connection == DRM_MODE_CONNECTED &&
                                  connector->encoder_id != 0;
        drmModeEncoderPtr encoder = connector_ok
                                        ? drmModeGetEncoder(fd, connector->encoder_id)
                                        : nullptr;
        const bool route_ok = encoder != nullptr && encoder->crtc_id == crtc_id;
        if(encoder != nullptr) drmModeFreeEncoder(encoder);
        if(connector != nullptr) drmModeFreeConnector(connector);

        drmModePlaneResPtr planes = drmModeGetPlaneResources(fd);
        drmModePlanePtr selected = nullptr;
        if(planes != nullptr) {
            for(uint32_t index = 0; index < planes->count_planes; ++index) {
                if(planes->planes[index] == plane_id) selected = drmModeGetPlane(fd, plane_id);
            }
        }
        bool format_ok = false;
        if(selected != nullptr) {
            for(uint32_t index = 0; index < selected->count_formats; ++index) {
                if(selected->formats[index] == framebuffer_format) format_ok = true;
            }
        }
        bool overlay_type_ok = false;
        bool zpos_ok = false;
        drmModeObjectPropertiesPtr properties =
            drmModeObjectGetProperties(fd, plane_id, DRM_MODE_OBJECT_PLANE);
        if(properties != nullptr) {
            for(uint32_t index = 0; index < properties->count_props; ++index) {
                drmModePropertyPtr property = drmModeGetProperty(fd, properties->props[index]);
                if(property != nullptr && std::strcmp(property->name, "type") == 0 &&
                   properties->prop_values[index] == DRM_PLANE_TYPE_OVERLAY) {
                    overlay_type_ok = true;
                }
                if(property != nullptr && std::strcmp(property->name, "zpos") == 0 &&
                   properties->prop_values[index] == static_cast<uint64_t>(overlay_zpos)) {
                    zpos_ok = true;
                }
                if(property != nullptr) drmModeFreeProperty(property);
            }
            drmModeFreeObjectProperties(properties);
        }
        const bool plane_ok = crtc_index >= 0 && crtc_index < 32 && selected != nullptr &&
                              (selected->possible_crtcs & (1U << crtc_index)) != 0 && format_ok;
        if(selected != nullptr) drmModeFreePlane(selected);
        if(planes != nullptr) drmModeFreePlaneResources(planes);
        drmModeFreeResources(resources);
        if(!connector_ok || !route_ok || !plane_ok || !overlay_type_ok || !zpos_ok) {
            errno = ENODEV;
            return fail("certified overlay target");
        }
        return true;
    }

    bool choose_connector_and_crtc()
    {
        drmModeResPtr resources = drmModeGetResources(fd);
        if(!resources) return fail("drmModeGetResources");

        drmModeConnectorPtr selected = nullptr;
        for(int index = 0; index < resources->count_connectors; ++index) {
            drmModeConnectorPtr connector = drmModeGetConnector(fd, resources->connectors[index]);
            if(!connector) continue;
            const std::string name = std::string(connector_type_name(connector->connector_type)) +
                                     "-" + std::to_string(connector->connector_type_id);
            if(name == profile.connector_name && connector->connection == DRM_MODE_CONNECTED &&
               connector->count_modes > 0) {
                selected = connector;
                break;
            }
            drmModeFreeConnector(connector);
        }

        if(!selected) {
            drmModeFreeResources(resources);
            errno = ENODEV;
            return fail("connected connector");
        }

        connector_id = selected->connector_id;
        mode = selected->modes[0];
        drmModeEncoderPtr encoder = nullptr;
        for(int index = 0; index < resources->count_encoders; ++index) {
            drmModeEncoderPtr candidate = drmModeGetEncoder(fd, resources->encoders[index]);
            if(candidate && candidate->encoder_id == selected->encoder_id) {
                encoder = candidate;
                break;
            }
            if(candidate) drmModeFreeEncoder(candidate);
        }

        if(!encoder) {
            drmModeFreeConnector(selected);
            drmModeFreeResources(resources);
            errno = ENODEV;
            return fail("connector encoder");
        }

        crtc_id = encoder->crtc_id;
        if(crtc_id == 0 && encoder->possible_crtcs != 0) {
            for(int index = 0; index < resources->count_crtcs; ++index) {
                if((encoder->possible_crtcs & (1U << index)) != 0) {
                    crtc_id = resources->crtcs[index];
                    break;
                }
            }
        }
        drmModeFreeEncoder(encoder);
        drmModeFreeConnector(selected);
        drmModeFreeResources(resources);

        if(crtc_id == 0) {
            errno = ENODEV;
            return fail("connector CRTC");
        }
        saved_crtc = drmModeGetCrtc(fd, crtc_id);
        if(!saved_crtc) return fail("drmModeGetCrtc");
        return true;
    }

    bool allocate_buffer(Buffer& buffer)
    {
        drm_mode_create_dumb create {};
        create.height = static_cast<uint32_t>(mode.vdisplay);
        create.width = static_cast<uint32_t>(mode.hdisplay);
        create.bpp = 32;
        if(ioctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create) != 0) return fail("CREATE_DUMB");

        buffer.handle = create.handle;
        buffer.pitch = create.pitch;
        buffer.size = create.size;
        if(overlay_mode) {
            const uint32_t handles[] {buffer.handle, 0, 0, 0};
            const uint32_t pitches[] {buffer.pitch, 0, 0, 0};
            const uint32_t offsets[] {0, 0, 0, 0};
            if(drmModeAddFB2(fd, mode.hdisplay, mode.vdisplay, framebuffer_format,
                             handles, pitches, offsets, &buffer.framebuffer, 0) != 0) {
                return fail("drmModeAddFB2 overlay");
            }
        }
        else if(drmModeAddFB(fd, mode.hdisplay, mode.vdisplay, 24, 32, buffer.pitch,
                             buffer.handle, &buffer.framebuffer) != 0) {
            return fail("drmModeAddFB");
        }

        drm_mode_map_dumb map {};
        map.handle = buffer.handle;
        if(ioctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map) != 0) return fail("MAP_DUMB");
        void* mapped = mmap(nullptr, buffer.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
                            static_cast<off_t>(map.offset));
        if(mapped == MAP_FAILED) return fail("mmap dumb buffer");
        buffer.map = static_cast<uint8_t*>(mapped);
        std::memset(buffer.map, 0, buffer.size);
        return true;
    }

    bool wait_for_flip()
    {
        drmEventContext event_context {};
        event_context.version = DRM_EVENT_CONTEXT_VERSION;
        event_context.page_flip_handler = page_flip_handler;

        while(flip_pending) {
            pollfd poll_fd {fd, POLLIN, 0};
            const int result = poll(&poll_fd, 1, 1000);
            if(result <= 0) {
                if(result == 0) errno = ETIMEDOUT;
                return fail("page flip wait");
            }
            if(drmHandleEvent(fd, &event_context) != 0) return fail("drmHandleEvent");
        }
        return true;
    }

    void release_buffer(Buffer& buffer)
    {
        if(buffer.map) munmap(buffer.map, buffer.size);
        if(buffer.framebuffer) drmModeRmFB(fd, buffer.framebuffer);
        if(buffer.handle) {
            struct drm_mode_destroy_dumb destroy {buffer.handle};
            ioctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
        }
        buffer = {};
    }
};

DrmBackend::DrmBackend(const DeviceProfile& profile)
    : impl_(std::make_unique<Impl>(profile))
{
}

DrmBackend::~DrmBackend()
{
    close();
}

bool DrmBackend::open()
{
    if(impl_->fd >= 0) return true;
    if(!impl_->configure_overlay()) return false;
    if(impl_->overlay_mode) {
        if(!impl_->open_inherited()) return false;
    } else {
        impl_->fd = ::open(impl_->profile.drm_device, O_RDWR | O_CLOEXEC);
        if(impl_->fd < 0) return impl_->fail("open DRM device");
    }

    uint64_t dumb = 0;
    if(drmGetCap(impl_->fd, DRM_CAP_DUMB_BUFFER, &dumb) != 0 || dumb == 0) {
        if(errno == 0) errno = ENOTSUP;
        close();
        return impl_->fail("DRM dumb buffers");
    }
    if(impl_->overlay_mode ? !impl_->validate_overlay_target()
                           : !impl_->choose_connector_and_crtc()) {
        close();
        return false;
    }
    impl_->buffers.resize(2);
    for(auto& buffer : impl_->buffers) {
        if(!impl_->allocate_buffer(buffer)) {
            close();
            return false;
        }
    }
    return true;
}

bool DrmBackend::present_logical(const uint32_t* pixels, int32_t width, int32_t height)
{
    if(!is_open() || pixels == nullptr || width != impl_->profile.logical_width ||
       height != impl_->profile.logical_height) {
        errno = EINVAL;
        return impl_->fail("present arguments");
    }
    if(impl_->flip_pending && !impl_->wait_for_flip()) return false;

    const int next = impl_->active_buffer == 0 ? 1 : 0;
    Buffer& buffer = impl_->buffers[static_cast<size_t>(next)];
    std::memset(buffer.map, 0, buffer.size);
    auto* output = reinterpret_cast<uint32_t*>(buffer.map);
    const uint32_t stride = buffer.pitch / sizeof(uint32_t);
    if(impl_->overlay_mode) {
        for(int32_t y = 0; y < height; ++y) {
            for(int32_t x = 0; x < width; ++x) {
                int32_t target_x = x;
                int32_t target_y = y;
                if(impl_->rotation == 90) {
                    target_x = height - 1 - y;
                    target_y = x;
                } else if(impl_->rotation == 180) {
                    target_x = width - 1 - x;
                    target_y = height - 1 - y;
                } else if(impl_->rotation == 270) {
                    target_x = y;
                    target_y = width - 1 - x;
                }
                output[target_y * stride + target_x] = pixels[y * width + x];
            }
        }
        if(drmModeSetPlane(
               impl_->fd, impl_->plane_id, impl_->crtc_id, buffer.framebuffer, 0,
               impl_->overlay_x, impl_->overlay_y,
               static_cast<uint32_t>(impl_->overlay_width),
               static_cast<uint32_t>(impl_->overlay_height), 0, 0,
               static_cast<uint32_t>(impl_->buffer_width) << 16U,
               static_cast<uint32_t>(impl_->buffer_height) << 16U) != 0) {
            return impl_->fail("drmModeSetPlane overlay");
        }
        impl_->modeset_done = true;
        impl_->active_buffer = next;
        return true;
    }
    for(int32_t y = 0; y < height; ++y) {
        const int32_t physical_x = impl_->profile.display_cross_axis_offset + y;
        for(int32_t x = 0; x < width; ++x) {
            const int32_t physical_y = width - 1 - x;
            output[physical_y * stride + physical_x] = pixels[y * width + x];
        }
    }

    if(!impl_->modeset_done) {
        if(drmModeSetCrtc(impl_->fd, impl_->crtc_id, buffer.framebuffer, 0, 0,
                          &impl_->connector_id, 1, &impl_->mode) != 0) {
            return impl_->fail("drmModeSetCrtc");
        }
        impl_->modeset_done = true;
        impl_->active_buffer = next;
        return true;
    }

    impl_->flip_pending = true;
    if(drmModePageFlip(impl_->fd, impl_->crtc_id, buffer.framebuffer, kPageFlipFlags, &impl_->flip_pending) != 0) {
        const int page_flip_errno = errno;
        impl_->flip_pending = false;
        if(page_flip_errno == EINVAL && impl_->page_flip_recoveries < kMaxPageFlipRecoveries) {
            if(drmModeSetCrtc(impl_->fd, impl_->crtc_id, buffer.framebuffer, 0, 0,
                              &impl_->connector_id, 1, &impl_->mode) == 0) {
                ++impl_->page_flip_recoveries;
                impl_->active_buffer = next;
                std::fprintf(stderr, "POC warn=page_flip_recovered errno=%d count=%u\n",
                             page_flip_errno, impl_->page_flip_recoveries);
                return true;
            }
            return impl_->fail("drmModeSetCrtc recovery");
        }
        errno = page_flip_errno;
        return impl_->fail("drmModePageFlip");
    }
    if(!impl_->wait_for_flip()) return false;
    impl_->active_buffer = next;
    return true;
}

void DrmBackend::close()
{
    if(!impl_ || impl_->fd < 0) return;
    if(impl_->flip_pending) impl_->wait_for_flip();
    if(impl_->overlay_mode && impl_->modeset_done) {
        drmModeSetPlane(impl_->fd, impl_->plane_id, impl_->crtc_id, 0, 0,
                        0, 0, 0, 0, 0, 0, 0, 0);
    }
    else if(impl_->saved_crtc && impl_->saved_crtc->mode_valid) {
        drmModeSetCrtc(impl_->fd, impl_->saved_crtc->crtc_id, impl_->saved_crtc->buffer_id,
                       impl_->saved_crtc->x, impl_->saved_crtc->y,
                       &impl_->connector_id, 1, &impl_->saved_crtc->mode);
    }
    for(auto& buffer : impl_->buffers) impl_->release_buffer(buffer);
    impl_->buffers.clear();
    if(impl_->saved_crtc) drmModeFreeCrtc(impl_->saved_crtc);
    impl_->saved_crtc = nullptr;
    ::close(impl_->fd);
    impl_->fd = -1;
    impl_->active_buffer = -1;
    impl_->modeset_done = false;
    impl_->overlay_mode = false;
    impl_->page_flip_recoveries = 0;
}

bool DrmBackend::is_open() const
{
    return impl_ && impl_->fd >= 0;
}

bool DrmBackend::has_presented_frame() const
{
    return impl_ && impl_->modeset_done;
}

const char* DrmBackend::last_error() const
{
    return impl_ ? impl_->error.c_str() : "backend not initialized";
}

}  // namespace dictpen
