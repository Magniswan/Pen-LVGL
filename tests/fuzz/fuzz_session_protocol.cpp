#include "lvgl_platform/application_registry.h"
#include "lvgl_platform/device_profile.h"
#include "lvgl_platform/installer_protocol.h"
#include "lvgl_platform/session_control.h"
#include "lvgl_platform/session_profile.h"
#include "lvgl_platform/storage_protocol.h"
#include "lvgl_platform/touch_protocol.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    lvgl_platform::SessionControlMessage control;
    (void)lvgl_platform::decode_session_control(data, size, control);
    (void)lvgl_platform::decode_application_registry(data, size);

    lvgl_platform::InstallerRequest installer_request;
    lvgl_platform::InstallerResponse installer_response;
    (void)lvgl_platform::decode_installer_request(data, size, installer_request);
    (void)lvgl_platform::decode_installer_response(data, size, installer_response);

    lvgl_platform::StorageRequest request;
    lvgl_platform::StorageResponse response;
    (void)lvgl_platform::decode_storage_request(data, size, request);
    (void)lvgl_platform::decode_storage_response(data, size, response);
    (void)lvgl_platform::decode_touch_frame(data, size, 960, 266);

    const char* text = size == 0 ? "" : reinterpret_cast<const char*>(data);
    (void)lvgl_platform::parse_session_profile(std::string_view(text, size));
    (void)lvgl_platform::parse_device_profile(std::string_view(text, size));
    return 0;
}
