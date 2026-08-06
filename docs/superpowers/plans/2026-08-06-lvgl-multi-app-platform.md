# LVGL Multi-App Platform Implementation Plan

## Goal

Turn the existing single native LVGL process into a supervised multi-app
session with a paged LVGL launcher, a shared pull-down status panel, the
existing PoC application, and a new single-page focus timer.

## Locked Inputs

- Approved design: `docs/superpowers/specs/2026-08-06-lvgl-multi-app-platform-design.md`
- Device profile: `OVERHEAD_Y01_SKU_CHN_PRO`, firmware 4.8.6
- Logical display: 960 x 266
- LVGL: 9.5.0
- Toolchain: existing AArch64 GCC 11.3 CMake toolchain
- Falcon launcher and recovery supervisor: preserve the current opt-in model

## Task 1: Add the Session Protocol and Registry

Files:

- Add `src/session/app_registry.{h,cpp}`
- Add `src/session/session_protocol.{h,cpp}`
- Add `src/session/session_main.cpp`
- Modify `CMakeLists.txt`

Work:

1. Define stable application IDs for launcher, PoC, and focus timer.
2. Store executable target names and display metadata in one static registry.
3. Define a fixed, versioned socket message for launch, home, and exit-session.
4. Implement a session loop using `socketpair`, `fork`, `exec`, `poll`, and
   exact child PID ownership.
5. Return to the launcher after an app crash and terminate after three
   consecutive launcher failures.
6. Accept the release bin directory as a fixed startup argument from the
   supervisor; never accept child-provided paths.

Verification:

- compile with warnings as errors;
- reject unknown IDs and malformed protocol messages; and
- fixture-run the session with test child executables where host execution is
  available.

## Task 2: Extract the Shared Platform Runtime

Files:

- Add `src/runtime/platform_runtime.{h,cpp}`
- Add `src/runtime/app_control.{h,cpp}`
- Modify `src/lvgl_poc.cpp`
- Modify `CMakeLists.txt`

Work:

1. Move DRM, evdev, LVGL display/input registration, main-loop scheduling,
   capture support, metrics, and ordered cleanup out of `lvgl_poc.cpp`.
2. Expose an application callback interface for UI construction and stop
   requests.
3. Read the inherited control FD and provide typed transition helpers.
4. Preserve existing `POC_RUN_SECONDS`, `POC_FAIL_AFTER`, capture, and metric
   behavior for regression tests.
5. Make signal handling and error return codes deterministic for all apps.

Verification:

- rebuild the migrated PoC;
- confirm its ELF dependencies and no RPATH/RUNPATH; and
- run existing capture and bounded-duration checks when device access exists.

## Task 3: Implement the Common Theme and Application Shell

Files:

- Add `src/shell/app_theme.{h,cpp}`
- Add `src/shell/app_shell.{h,cpp}`
- Add focused shell state tests where practical
- Modify `lv_conf.h` only if an additional built-in font is required

Work:

1. Centralize colors, dimensions, text styles, and button styling.
2. Create the top-edge gesture area and panel on `lv_layer_top()`.
3. Implement closed, dragging, open, and settling states with axis locking.
4. Show time, battery state, current app, Home, and Exit to Falcon.
5. Route Home and Exit through `app_control`; confirm only Exit to Falcon.
6. Ensure timers, animations, and callbacks are released with the shell.

Verification:

- exercise gesture thresholds and horizontal/vertical arbitration;
- verify all controls are at least 44 logical pixels; and
- confirm the shell can be created and destroyed repeatedly.

## Task 4: Build the Native Paged Launcher

Files:

- Add `apps/launcher/launcher_main.cpp`
- Add `apps/launcher/launcher_ui.{h,cpp}`
- Add launcher icon resources or LVGL symbol mappings
- Modify `CMakeLists.txt`

Work:

1. Render a fixed 960 x 266 launcher using the shared runtime and shell.
2. Build the application list from the shared registry, excluding the launcher.
3. Use a horizontal snap pager and page indicators from the first release.
4. Show fixed-size entries with icon, name, and compact status.
5. Send a typed launch command and show a stable transition state on selection.

Verification:

- verify paging and selection callbacks;
- capture the first frame for visual inspection; and
- confirm long labels cannot resize the layout.

## Task 5: Migrate the PoC and Add the Focus Timer

Files:

- Move/adapt `src/app/poc_ui.{h,cpp}` under `apps/poc/`
- Add `apps/poc/poc_main.cpp`
- Add `apps/focus_timer/focus_timer_main.cpp`
- Add `apps/focus_timer/focus_timer_model.{h,cpp}`
- Add `apps/focus_timer/focus_timer_ui.{h,cpp}`
- Update font resources for the final UTF-8 string set
- Modify `CMakeLists.txt`

Work:

1. Make the PoC business UI use the shared runtime and shell.
2. Correct all user-visible mojibake and remove the old app-local exit bar.
3. Implement monotonic 15, 25, and 45 minute timer presets.
4. Implement start/pause, reset, completion, and repeat interactions.
5. Keep status-panel use independent from timer state.

Verification:

- unit-check timer drift after delayed callbacks;
- capture launcher, PoC, and timer screens; and
- verify Home and Exit commands from all apps.

## Task 6: Integrate the Device Release and Falcon Supervisor

Files:

- Modify `launcher/device/lvgl-supervisor.sh`
- Modify `launcher/device/manifest.template.env`
- Modify `scripts/install_device_app.ps1`
- Modify `scripts/status_device_app.ps1`
- Modify `scripts/uninstall_device_app.ps1` if process matching changes
- Modify `scripts/tests/test_supervisor.ps1`
- Modify `launcher/native/src/Launcher/Launcher.cpp`

Work:

1. Validate and launch `lvgl_session` instead of `lvgl_poc`.
2. Validate hashes for the session, launcher, PoC, focus timer, supervisor, and
   required assets before stopping Falcon.
3. Package all binaries under the versioned release `bin/` directory.
4. Keep duplicate launch, exact process matching, lock, log rotation, and
   idempotent Falcon restoration behavior unchanged.
5. Expand status output to show the session and active application.

Verification:

- run shell syntax and supervisor fixture tests;
- run PowerShell parser checks; and
- test missing or mismatched multi-app payloads before Falcon handoff.

## Task 7: Build, Inspect, and Document

Files:

- Update `README.md`
- Add or update evaluation notes after real-device results

Work:

1. Configure and build all AArch64 targets with Ninja.
2. Run `git diff --check` and focused local tests.
3. Inspect every ELF for architecture, interpreter, dependencies, and RPATH.
4. If a matching device is connected, install and exercise launcher paging,
   panel gestures, app switching, timer completion, abnormal app exit, and
   Falcon restoration.
5. Record any device-only verification that could not be run.

Acceptance:

- all targets build with warnings as errors;
- the package validates every runtime artifact;
- one active LVGL child exists at a time;
- all three LVGL UIs use the shared status panel;
- app crashes return to the launcher;
- ending the session restores Falcon; and
- no unrelated user files or guardian services are changed.
