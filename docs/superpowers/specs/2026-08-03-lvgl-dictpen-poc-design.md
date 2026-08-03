# Youdao Y01 LVGL PoC Design

## Objective

Build and validate a standalone LVGL user-interface proof of concept on the connected Youdao dictionary pen. The PoC will temporarily stop Falcon, acquire the display and touch input directly, run a focused UI and performance test, then restore Falcon without changing persistent firmware or boot configuration.

The work is complete at M5 when real-device evidence establishes whether the LVGL route is suitable for continued product development.

## Target Device Profile

The initial profile is limited to the probed device and firmware:

- Hardware profile: `OVERHEAD_Y01_SKU_CHN_PRO`
- SoC and ABI: Rockchip RK3562, AArch64
- Operating system: Buildroot 2021.11, Linux 5.10.160
- C runtime: glibc 2.36
- Firmware: 4.8.6, built 2025-11-13
- Display: DRM/KMS, connector `DSI-1`, physical mode 480x960 at 60 Hz
- Falcon logical area: 960x266, rotation 270 degrees, vertical offset 107
- Touch device: evdev device named `hyn_ts`, currently `/dev/input/event4`
- Writable executable storage: `/tmp`, `/userdata`, and `/userdisk`

Runtime discovery must select the connector, CRTC, primary plane, and touch node by properties and device name. Numeric DRM object ids and `/dev/input/event4` must not become cross-device defaults.

## Scope

The PoC validates:

- DRM/KMS display acquisition, dumb buffers, double buffering, and page flips
- evdev touch input and raw-to-logical coordinate conversion
- LVGL controls, page switching, scrolling, dragging, long press, and rapid taps
- Chinese and English text, PNG images, opacity, rounded shapes, and local animation
- FPS, frame time, resident memory, and dirty-region diagnostics
- reliable transition from Falcon to the PoC and back to Falcon
- recovery after normal exit, PoC failure, and timeout

The PoC does not integrate scanning, OCR, audio, networking, the Youdao input method, Wi-Fi settings, OTA, or other proprietary services. It does not modify the EROFS root filesystem, system partitions, boot scripts, installed mini-apps, or existing user data. Temporary files are limited to `/tmp`; normal firmware logging may continue while Falcon is restored.

## Selected Approach

Use a pinned LVGL release, built from source and statically linked into an AArch64 C++ executable. Do not link the firmware's version-unknown `/usr/lib/liblvgl.so`.

Use DRM/KMS directly for software-rendered output and evdev directly for touch. The first version does not require Qt, SDL, Wayland, EGL, GLES, GBM, Flutter, or a browser runtime.

The application may dynamically use the target's glibc and libdrm when the selected cross-toolchain and sysroot guarantee compatibility. All remaining small dependencies should be pinned and built with the project.

## Architecture

The implementation has four layers:

1. Test application: three PoC pages and their test state.
2. LVGL UI layer: components, styles, theme, page navigation, and resources.
3. Platform layer: DRM display, evdev input, coordinate conversion, timing, and diagnostics.
4. Device profile: Y01-specific geometry and discovery constraints.

UI code must not open `/dev/dri` or `/dev/input` directly. Platform interfaces must expose display initialization, frame submission, input polling, timing, and diagnostic snapshots without exposing device-specific ids to page code.

### Proposed Repository Layout

```text
lvgl-dictpen-poc/
  CMakeLists.txt
  cmake/toolchain-aarch64.cmake
  third_party/lvgl/
  src/
    app/
    platform/drm/
    platform/input/
    platform/device_profile/
    diagnostics/
  assets/
  scripts/
  tests/
  docs/superpowers/specs/
```

## Display Design

At startup, the DRM backend opens `/dev/dri/card0`, locates the connected `DSI-1` connector, resolves its active or preferred mode, locates the compatible CRTC, and chooses a plane whose type is Primary. It records the pre-PoC mode state before changing display resources.

Rendering uses two DRM dumb buffers. LVGL flush callbacks copy only invalidated areas into the back buffer. The backend then issues a page flip and waits for completion before reusing a buffer. The implementation must handle interrupted waits and reject buffer reuse while a flip is pending.

The logical LVGL canvas is 960x266. The platform layer owns the transformation between that logical landscape canvas and the 480x960 physical scanout, including the 270-degree orientation and visible-area offset. Coordinate transforms require host-side corner and boundary tests.

## Input Design

The input backend enumerates evdev devices and chooses the device whose name is `hyn_ts`. It parses absolute touch position, contact state, and synchronization events. Device events are converted into LVGL pointer state through a pure coordinate-transform function.

The transform is configured by the Y01 profile and tested for the four corners, edges, center, clamping, rotation, and offsets. Raw coordinates and transformed coordinates are visible on the diagnostics page.

The first PoC supports a single logical pointer. Multi-touch gestures are out of scope even if the hardware reports multiple slots.

## User Interface

The 960x266 interface contains a persistent top bar and three test pages:

- Interaction: buttons, switch, slider, horizontal and vertical scrolling, long press, drag, and rapid tap counters.
- Visual: Chinese and English samples, several font sizes, PNG rendering, opacity, rounded shapes, a transition, and a continuously animated local region.
- Diagnostics: raw and logical touch coordinates, FPS, frame time, resident memory, refreshed area, and full-screen solid-color tests.

The top bar always shows FPS and resident memory. Exit requires confirmation. The visual design is restrained and high contrast; the PoC prioritizes legibility, spacing, component states, and touch feedback over decorative effects.

The Chinese font asset contains only the glyphs needed by the PoC. A full CJK font is not bundled during M0-M5.

## Runtime Data Flow

The event loop advances LVGL timers, consumes current pointer state, collects dirty rectangles, submits display updates, and records timing data. Input reading may use a dedicated thread or nonblocking file descriptor, but only one component owns the evdev descriptor.

Diagnostics read only the PoC process and frame loop. They must not continuously parse firmware logs or enumerate unrelated system processes.

## Falcon Handoff and Recovery

A host-side launcher performs preflight checks, uploads temporary artifacts, and starts a device-side recovery wrapper. The wrapper owns the Falcon handoff for the duration of the test.

The wrapper must:

1. Identify the `guardian_run` instance whose child command is `/usr/bin/runDictPen`.
2. Record the relevant process identities before changing state.
3. Stop that guardian instance before stopping `/usr/bin/miniapp`.
4. Wait until Falcon releases the display before starting the PoC.
5. Run the PoC with a default five-minute timeout.
6. Restore `/usr/bin/guardian_run /usr/bin/runDictPen` after normal exit, failure, signal, or timeout.
7. Verify that a new `/usr/bin/miniapp` process appears and remains alive long enough to indicate successful recovery.

The wrapper must never use a global `killall guardian_run`, because other guardian instances supervise Wi-Fi, audio, Bluetooth, OTA, and input services. Repeatedly killing only `miniapp` is also forbidden because `runDictPen` includes firmware crash-count and reboot behavior.

Artifacts run from `/tmp` during M0-M5. No persistent autostart mechanism is introduced.

## Error Handling

- Profile mismatch: stop before changing device state and print the observed profile.
- Missing DRM connector, CRTC, or primary plane: return a diagnostic error and restore Falcon.
- Touch device missing: allow a display-only diagnostic page only when explicitly selected; otherwise restore Falcon.
- Page flip timeout: stop rendering, release DRM resources, and restore Falcon.
- PoC crash or nonzero exit: preserve logs in the host test-results directory and restore Falcon.
- ADB interruption: the device-side wrapper's signal and timeout handlers perform recovery independently of the host shell.
- Falcon recovery failure: retry a bounded number of times, report the observed process tree and logs, then leave the device reachable through ADB without rebooting it.

## Build Strategy

Use CMake with an explicitly selected AArch64 toolchain. Prefer the matching RK3562 Buildroot SDK toolchain and sysroot. A generic toolchain is acceptable only after verifying the produced ELF interpreter, minimum glibc requirement, `DT_NEEDED` entries, and symbol versions against the device.

The build must verify:

- ELF64, little-endian, AArch64
- interpreter `/lib/ld-linux-aarch64.so.1` when dynamically linked
- no dependency on unavailable Qt, SDL, EGL, GLES, GBM, Wayland, or X11 libraries
- pinned LVGL source revision and configuration
- deterministic inclusion of the required PoC assets

## Test Strategy

Host tests cover device-profile parsing, DRM-selection logic using fixtures, coordinate conversion, boundary clamping, and performance-stat calculations. Script tests cover process-tree selection and ensure unrelated guardian processes are never targeted.

Real-device validation covers:

- pure colors and orientation
- all four touch corners and edge tracking
- button, switch, slider, list scrolling, drag, long press, and rapid taps
- Chinese text, image rendering, opacity, rounded shapes, transition, and local animation
- normal exit, forced failure, timeout, and Falcon recovery
- five complete Falcon-to-PoC-to-Falcon cycles
- a five-minute continuous run with collected FPS, frame-time, CPU, and RSS data

## Milestones

### M0: Build Baseline

Cross-compile a minimal executable, verify its ELF metadata and dependencies, and execute a non-display diagnostic command on the target.

### M1: Display

Acquire DRM after the controlled Falcon handoff and show correctly oriented solid colors using double buffering and page flips.

### M2: Input

Read `hyn_ts`, validate raw and logical coordinates, and demonstrate accurate taps, dragging, and edge tracking.

### M3: LVGL Interface

Deliver the interaction, visual, and diagnostics pages with the persistent status bar and confirmed exit flow.

### M4: Stability and Recovery

Implement timeout, signal handling, failure recovery, host log collection, and verified Falcon restoration.

### M5: Real-Device Evaluation

Run the complete validation matrix, summarize observed performance and failures, and issue a go/no-go recommendation for a long-term LVGL application platform.

## Acceptance Criteria

- Animated test remains at or above 30 FPS; ordinary interaction targets approximately 60 FPS.
- PoC resident memory remains at or below 40 MB.
- Four corners, edges, drag, and long press map correctly without visible discontinuities.
- Five-minute run completes without crash, black screen, or input loss.
- Five Falcon handoff and recovery cycles complete successfully.
- Normal exit, forced PoC failure, and timeout all restore Falcon.
- No persistent system partition, boot configuration, installed mini-app, or existing user-data mutation occurs. PoC artifacts remain temporary, and firmware-generated logs are not treated as PoC data changes.

If a threshold is missed, M5 records the measured result and cause. The recommendation may still be conditional when the cause is isolated and has a credible low-risk fix; it must not silently relax the threshold.

## M5 Decision Rule

Recommend continuing to a long-term LVGL platform only when display and input are correct, Falcon recovery is reliable, and performance meets the acceptance criteria or misses them only for a documented and readily correctable reason. Proprietary service integration remains a separate follow-up project and is not evidence required for the M5 decision.
