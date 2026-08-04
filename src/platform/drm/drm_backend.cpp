#include "platform/drm/drm_backend.h"

#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
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
    drmModeModeInfo mode {};
    drmModeCrtcPtr saved_crtc {nullptr};
    std::vector<Buffer> buffers;
    int active_buffer {-1};
    bool flip_pending {false};
    bool modeset_done {false};
    std::string error;

    bool fail(const char* operation)
    {
        error = std::string(operation) + ": " + std::strerror(errno);
        return false;
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
        if(drmModeAddFB(fd, mode.hdisplay, mode.vdisplay, 24, 32, buffer.pitch,
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
    impl_->fd = ::open(impl_->profile.drm_device, O_RDWR | O_CLOEXEC);
    if(impl_->fd < 0) return impl_->fail("open DRM device");

    uint64_t dumb = 0;
    if(drmGetCap(impl_->fd, DRM_CAP_DUMB_BUFFER, &dumb) != 0 || dumb == 0) {
        if(errno == 0) errno = ENOTSUP;
        close();
        return impl_->fail("DRM dumb buffers");
    }
    if(!impl_->choose_connector_and_crtc()) {
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
        impl_->flip_pending = false;
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
    if(impl_->saved_crtc && impl_->saved_crtc->mode_valid) {
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
