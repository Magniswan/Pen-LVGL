#include "lvgl_platform/application_registry.h"
#include "lvgl_platform/crypto_provider.h"
#include "lvgl_platform/installer_protocol.h"
#include "lvgl_platform/release_state.h"
#include "lvgl_platform/session_control.h"
#include "lvgl_platform/storage_protocol.h"
#include "lvgl_platform/touch_protocol.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

bool write_bytes(const std::filesystem::path& path, const std::uint8_t* bytes, std::size_t size)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if(!output) return false;
    output.write(reinterpret_cast<const char*>(bytes), static_cast<std::streamsize>(size));
    return output.good();
}

bool write_bytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
{
    return write_bytes(path, bytes.data(), bytes.size());
}

}  // namespace

int main(int argc, char** argv)
{
    if(argc != 2) return 2;
    const std::filesystem::path root(argv[1]);
    const auto state_root = root / "state";
    const auto session_root = root / "session";
    std::error_code error;
    std::filesystem::create_directories(state_root, error);
    if(error) return 3;
    std::filesystem::create_directories(session_root, error);
    if(error) return 3;

    const auto crypto = lvgl_platform::CryptoProvider::load_default();
    if(crypto == nullptr) return 4;
    lvgl_platform::ApplicationReleaseState state;
    state.app_id = "top.lvgl.fuzzseed";
    state.signing_key_id = std::string(32, '1');
    state.generation = 1;
    state.current_release = 1;
    state.high_release = 1;
    state.current_digest.fill(0x11);
    state.high_digest = state.current_digest;
    const auto encoded_state = lvgl_platform::encode_release_state(state, *crypto);
    if(!encoded_state.ok() || !write_bytes(state_root / "valid.state", encoded_state.bytes)) {
        return 5;
    }

    const auto control = lvgl_platform::encode_session_control(
        {lvgl_platform::SessionControlCommand::launch_application, 0,
         "top.lvgl.fuzzseed"});
    if(!write_bytes(session_root / "launch.control", control.data(), control.size())) return 6;

    const auto registry = lvgl_platform::encode_application_registry({
        {"top.lvgl.fuzzseed", "Fuzz Seed", "1.0.0", 1, 0, 0},
    });
    if(!registry.ok() || !write_bytes(session_root / "registry.bin", registry.bytes)) return 7;

    const auto storage = lvgl_platform::encode_storage_request(
        {lvgl_platform::StorageCommand::write, 1, "seed", 16, {0x01, 0x02}});
    if(storage.empty() || !write_bytes(session_root / "storage.request", storage)) return 8;

    const auto installed_scan = lvgl_platform::encode_installer_request(
        {lvgl_platform::InstallerCommand::installed_scan, 2, 0, {}});
    if(!write_bytes(
           session_root / "installer-installed-scan.request",
           installed_scan.data(), installed_scan.size())) {
        return 8;
    }
    const auto remove = lvgl_platform::encode_installer_request(
        {lvgl_platform::InstallerCommand::remove, 3, 0, std::string(128, 'a')});
    if(!write_bytes(
           session_root / "installer-remove.request", remove.data(), remove.size())) {
        return 8;
    }

    const auto touch = lvgl_platform::encode_touch_frame(
        {1, 1, 1, lvgl_platform::TouchPhase::start, 0, 100, 100, 0});
    if(!write_bytes(session_root / "touch.frame", touch.data(), touch.size())) return 9;

    const std::string profile =
        "PROFILE_ID=youdao-y01-4.8.6\n"
        "MACHINE=aarch64\n"
        "LOGICAL_WIDTH=960\n"
        "LOGICAL_HEIGHT=266\n"
        "DRM_DEVICE=/dev/dri/card0\n"
        "DRM_CONNECTOR_ID=1\n"
        "DRM_CRTC_ID=1\n"
        "DRM_OVERLAY_PLANE_ID=1\n"
        "DRM_OVERLAY_ZPOS=1\n"
        "DISPLAY_X=0\n"
        "DISPLAY_Y=0\n"
        "DISPLAY_WIDTH=960\n"
        "DISPLAY_HEIGHT=266\n"
        "PIXEL_FORMAT=XRGB8888\n"
        "DISPLAY_ROTATION=0\n"
        "HOLE_SESSION_CERTIFIED=1\n";
    if(!write_bytes(
           session_root / "profile.env",
           reinterpret_cast<const std::uint8_t*>(profile.data()), profile.size())) {
        return 10;
    }
    return 0;
}
