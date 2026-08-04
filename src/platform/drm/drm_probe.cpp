#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <iostream>
#include <string>

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>

namespace {

bool has_format(const drmModePlane* plane, uint32_t format)
{
    for(uint32_t index = 0; index < plane->count_formats; ++index) {
        if(plane->formats[index] == format) return true;
    }
    return false;
}

const char* connector_type_name(uint32_t type)
{
    switch(type) {
        case DRM_MODE_CONNECTOR_DSI: return "DSI";
        case DRM_MODE_CONNECTOR_DPI: return "DPI";
        case DRM_MODE_CONNECTOR_LVDS: return "LVDS";
        case DRM_MODE_CONNECTOR_eDP: return "eDP";
        case DRM_MODE_CONNECTOR_HDMIA: return "HDMI-A";
        case DRM_MODE_CONNECTOR_DisplayPort: return "DP";
        default: return "unknown";
    }
}

std::string plane_type(int fd, uint32_t plane_id)
{
    drmModeObjectPropertiesPtr props =
        drmModeObjectGetProperties(fd, plane_id, DRM_MODE_OBJECT_PLANE);
    if(!props) return "unknown";

    std::string result = "unknown";
    for(uint32_t index = 0; index < props->count_props; ++index) {
        drmModePropertyPtr property = drmModeGetProperty(fd, props->props[index]);
        if(!property) continue;
        if(std::strcmp(property->name, "type") == 0 &&
           property->count_enums > 0) {
            const uint64_t value = props->prop_values[index];
            for(int enum_index = 0; enum_index < property->count_enums; ++enum_index) {
                if(property->enums[enum_index].value == value) {
                    result = property->enums[enum_index].name;
                    break;
                }
            }
        }
        drmModeFreeProperty(property);
    }
    drmModeFreeObjectProperties(props);
    return result;
}

}  // namespace

int main()
{
    const int fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if(fd < 0) {
        std::cerr << "drm_probe.error=open: " << std::strerror(errno) << '\n';
        return 2;
    }

    uint64_t has_dumb = 0;
    if(drmGetCap(fd, DRM_CAP_DUMB_BUFFER, &has_dumb) != 0) {
        std::cerr << "drm_probe.error=dumb_capability: " << std::strerror(errno) << '\n';
        close(fd);
        return 3;
    }
    std::cout << "drm.dumb_buffer=" << has_dumb << '\n';

    drmVersionPtr version = drmGetVersion(fd);
    if(version) {
        std::cout << "drm.name=" << std::string(version->name, version->name_len) << '\n';
        drmFreeVersion(version);
    }

    drmModeResPtr resources = drmModeGetResources(fd);
    if(!resources) {
        std::cerr << "drm_probe.error=resources: " << std::strerror(errno) << '\n';
        close(fd);
        return 4;
    }

    bool connector_found = false;
    for(int index = 0; index < resources->count_connectors; ++index) {
        drmModeConnectorPtr connector = drmModeGetConnector(fd, resources->connectors[index]);
        if(!connector) continue;

        const std::string name = std::string(connector_type_name(connector->connector_type)) +
                                 "-" + std::to_string(connector->connector_type_id);
        std::cout << "connector.id=" << connector->connector_id
                  << " name=" << name
                  << " status=" << connector->connection
                  << " modes=" << connector->count_modes << '\n';
        if(name == "DSI-1" && connector->connection == DRM_MODE_CONNECTED &&
           connector->count_modes > 0) {
            connector_found = true;
            std::cout << "connector.selected=DSI-1"
                      << " mode=" << connector->modes[0].hdisplay << 'x'
                      << connector->modes[0].vdisplay << '\n';
        }
        drmModeFreeConnector(connector);
    }

    drmModePlaneResPtr planes = drmModeGetPlaneResources(fd);
    if(!planes) {
        std::cerr << "drm_probe.error=planes: " << std::strerror(errno) << '\n';
        drmModeFreeResources(resources);
        close(fd);
        return 5;
    }

    bool primary_found = false;
    for(uint32_t index = 0; index < planes->count_planes; ++index) {
        drmModePlanePtr plane = drmModeGetPlane(fd, planes->planes[index]);
        if(!plane) continue;
        const std::string type = plane_type(fd, plane->plane_id);
        const bool xrgb = has_format(plane, DRM_FORMAT_XRGB8888);
        std::cout << "plane.id=" << plane->plane_id
                  << " type=" << type
                  << " possible_crtcs=0x" << std::hex << plane->possible_crtcs
                  << std::dec << " xrgb8888=" << (xrgb ? 1 : 0) << '\n';
        primary_found = primary_found || (type == "Primary" && xrgb);
        drmModeFreePlane(plane);
    }

    drmModeFreePlaneResources(planes);
    drmModeFreeResources(resources);
    close(fd);

    if(!connector_found || !primary_found) {
        std::cerr << "drm_probe.result=FAIL connector=" << (connector_found ? 1 : 0)
                  << " primary_xrgb=" << (primary_found ? 1 : 0) << '\n';
        return 6;
    }
    std::cout << "drm_probe.result=PASS\n";
    return 0;
}
