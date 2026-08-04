# LVGL Dictionary Pen PoC

Standalone LVGL 9.5.0 proof of concept for the probed Youdao Y01 dictionary
pen firmware. It temporarily hands the DRM display and `hyn_ts` touch device
from Falcon to a native AArch64 process, then restores Falcon through a
device-side recovery wrapper.

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

Do not deploy this PoC as an init script or persistent launcher. Review
[docs/m5-evaluation.md](docs/m5-evaluation.md) before extending it.
