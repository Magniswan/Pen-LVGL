# LVGL Dictionary Pen PoC

Standalone LVGL 9.5.0 application and Falcon launcher for the probed Youdao
Y01 dictionary-pen firmware. A Falcon mini-app provides the desktop entry
point, then a device-side supervisor hands the DRM display and `hyn_ts` touch
device to the native AArch64 process. Falcon is restored after a normal exit,
application failure, or supervisor signal.

This repository is a hardware evaluation, not a general Falcon replacement.
The current profile is limited to `OVERHEAD_Y01_SKU_CHN_PRO`, firmware 4.8.6.

## What Is Implemented

- DRM/KMS connector and primary-plane discovery
- rotated 960x266 logical canvas on the 480x960 DSI panel
- double-buffered page flips and dirty-region rendering
- evdev touch discovery and coordinate conversion
- interaction, visual, and diagnostics pages
- runtime FPS, frame time, CPU, RSS, touch, and frame counters
- timeout, signal, failure, and normal-exit Falcon recovery
- uinput-driven automated interaction coverage
- Falcon icon and native JSAPI bridge for device-only launch
- versioned `/userdisk` payload, bounded logs, status, install, and uninstall

## Device-Only Use

The launcher is already installed on the validated device as `LVGL 应用`.
No ADB connection is needed for normal use:

1. Find `LVGL 应用` in Falcon's application list and tap its icon.
2. The short launcher page checks the installed native payload and switches to
   LVGL.
3. Tap the top-right exit control in LVGL and confirm to return to Falcon.

The launcher is opt-in. It does not add a boot hook and does not replace
Falcon, so restarting the pen always follows the original Falcon boot path.

## Build

The configured toolchain is ARM GNU 11.3 for AArch64. The target sysroot must
contain the device `libdrm.so.2` at `device-sysroot/usr/lib/`.

```sh
cmake -S . -B build/m5 -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-aarch64.cmake
cmake --build build/m5 --target lvgl_poc -j4
```

The final executable is `build/m5/lvgl_poc`. It is dynamically linked only to
libraries present in the profiled firmware and does not contain a development
host RPATH.

## M5 Device Run

Keep the pen powered and connected through ADB. The script uploads only
temporary `/tmp` artifacts, runs for five minutes, collects the wrapper log,
and verifies that Falcon's guardian and miniapp processes returned.

```powershell
powershell -ExecutionPolicy Bypass -File scripts/run_m5.ps1
```

To collect a test that continued on the device while ADB was interrupted:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/run_m5.ps1 -CollectOnly
```

## Falcon Launcher Build and Install

Use Node.js 16.20.2 with the locked `aiot-vue-cli` 1.0.32. Build the AArch64
native bridge before packaging the production AMR:

```sh
bash launcher/tools/build-native.sh
cd launcher
pnpm install --frozen-lockfile
pnpm build:prod
```

With exactly one matching pen connected through ADB:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/install_device_app.ps1
powershell -ExecutionPolicy Bypass -File scripts/status_device_app.ps1 -ShowLog
```

The installer refuses a different ABI, PCBA, firmware, screen profile, AppID
owner, or an active LVGL session. It verifies hashes before activating
`/userdisk/apps/lvgl-poc/current`.

To remove only this launcher, payload, runtime state, and dedicated logs:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/uninstall_device_app.ps1
```

Do not deploy the PoC through `/etc/init.d`, `/etc/inittab`, or another boot
hook. Review [docs/m5-evaluation.md](docs/m5-evaluation.md) and
[docs/launcher-evaluation.md](docs/launcher-evaluation.md) before extending it.
