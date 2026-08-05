# Falcon-to-LVGL Launcher Implementation Plan

## Goal

Install a Falcon launcher icon that starts the existing standalone LVGL app on
the profiled Y01 firmware, then restores Falcon after normal or abnormal LVGL
exit. Daily use must require no ADB.

## Locked Inputs

- Falcon builder: `aiot-vue-cli@1.0.32`
- Builder runtime: Node 16.20.2 at the existing workspace runtime path
- Native compiler: Buildroot 2021.11 AArch64 GCC 11.3.0
- Native SDK: `haasui-falcon-app/templates/jsapi/iot-miniapp-sdk`
- Launcher AppID: `8080992608050001`
- Target profile: `OVERHEAD_Y01_SKU_CHN_PRO`, firmware 4.8.6

## Task 1: Make LVGL Suitable for Interactive Launch

Files:

- Modify `src/lvgl_poc.cpp`
- Add or extend host checks under `scripts/tests/`

Work:

1. Define `POC_RUN_SECONDS=0` as unlimited runtime.
2. Preserve positive-duration M5 behavior and invalid-value fallback.
3. Keep the existing confirmed UI exit and signal paths.
4. Rebuild the AArch64 executable and rerun ELF metadata checks.

Verification:

```text
positive duration exits at the requested time
zero duration remains alive until TERM or UI exit
AArch64 / glibc / dependency requirements remain compatible
```

## Task 2: Implement the Device Supervisor

Files:

- Add `launcher/device/lvgl-supervisor.sh`
- Add `launcher/device/manifest.template.json`
- Add `scripts/tests/test_supervisor.ps1`

Work:

1. Implement exact `/proc/<pid>/cmdline` matching for the DictPen guardian,
   `runDictPen`, and `miniapp`.
2. Acquire an atomic `/run/lvgl-poc/lock` directory.
3. Validate the fixed current release and executable before handoff.
4. Stop only the Falcon UI chain, wait for release, and launch LVGL.
5. Trap `EXIT`, `TERM`, `INT`, and `HUP`; use one idempotent restore path.
6. Retry Falcon restoration at 1, 2, and 5 seconds.
7. Write bounded state and rotated logs.
8. Expose a fixture root/test mode so selection and lifecycle logic can be
   tested without touching real processes.

Verification:

```text
sh -n launcher/device/lvgl-supervisor.sh
PowerShell fixture tests for guardian selection, duplicate lock, failed exec,
normal exit, nonzero exit, and restore retry
```

## Task 3: Implement the Native Falcon Bridge

Files:

- Add `launcher/native/CMakeLists.txt`
- Vendor `launcher/native/iot-miniapp-sdk/` from the selected skill template
- Add `launcher/native/src/JSAPI.cpp`
- Add `launcher/native/src/Launcher/Launcher.{hpp,cpp}`
- Add `launcher/native/src/Launcher/JSLauncher.{hpp,cpp}`
- Add `launcher/tools/build-native.sh`

Work:

1. Export `custom_init_jsapis`, register module `lvgl_launcher`, and export
   class `Launcher`.
2. Provide `probe()`, `start()`, and `status()` Promise methods.
3. Accept no path or command arguments from JavaScript.
4. Validate the fixed supervisor path, regular-file mode, and profile manifest.
5. Fork, `setsid`, redirect file descriptors, and exec the supervisor after a
   short handoff delay.
6. Return structured Bson results and translate errors without blocking the JS
   thread.

Verification:

```text
ELF64 AArch64 shared object
module name / library name / loader name match
custom_init_jsapis exported
no host RPATH and only target-compatible dynamic dependencies
```

## Task 4: Implement the Falcon Launcher Mini-App

Files:

- Add `launcher/package.json`
- Add `launcher/src/app.js`, `app.json`, and `base-page.js`
- Add `launcher/src/pages/index/index.{js,vue}`
- Add `launcher/src/services/launcher.js`
- Add `launcher/api-mock/lvgl_launcher.js`
- Add `launcher/libs/arm64-orange/libjsapi_lvgl_launcher.so` during build
- Add `launcher/app_icon.png` from the existing PoC visual asset

Work:

1. Use viewport width 960 and the existing BasePage lifecycle pattern.
2. On first page show, call `probe()` and then `start()` once per generation.
3. Render compact launching, error/retry, and already-running states.
4. Keep all timer and native-operation cleanup idempotent on unload.
5. Package `app_icon.png` at archive root and the native library under the
   firmware-selected `arm64-orange` directory.

Verification:

```text
aiot-cli development and production builds on Node 16.20.2
manifest AppID, app name, icon, QuickJS version, and native library certificate
simulator/mock state behavior where supported
```

## Task 5: Implement Portable Host Operations

Files:

- Add `scripts/install_device_app.ps1`
- Add `scripts/status_device_app.ps1`
- Add `scripts/uninstall_device_app.ps1`
- Add shared `scripts/device_app_common.ps1`

Work:

1. Require exactly one ADB device and match model, firmware, ABI, and free
   space before mutation.
2. Reject AppID collisions with a different installed package.
3. Build a versioned release manifest containing SHA-256 values.
4. Push to a `.staging` directory, validate on-device hashes and modes, then
   atomically activate `current` while preserving one rollback release.
5. Install the `.amr` through `miniapp_cli install`.
6. Provide status output for release, state, PIDs, and bounded logs.
7. Uninstall the mini-app, stop only matching launcher processes, and remove
   only the dedicated app and log directories.

Verification:

```text
PowerShell parser checks
dry-run/preflight checks
install, reinstall, status, and uninstall against the target device
```

## Task 6: Build and Real-Device Validation

Work:

1. Build LVGL, native bridge, and production `.amr` from locked tools.
2. Install the release and verify that the Falcon icon appears.
3. Start from the icon, verify LVGL display/touch, and exit to Falcon.
4. Repeat twice and verify no residual processes or memory growth.
5. Exercise duplicate start, missing payload, LVGL injected failure, and
   supervisor signal cleanup.
6. Reboot once while Falcon is active and once while LVGL is active; both must
   return to Falcon because no boot hook is installed.
7. Run uninstall/reinstall and confirm unrelated Falcon data and guardian
   processes remain.
8. Capture logs, process trees, hashes, and screenshots under
   `test-results/launcher/`.

## Task 7: Documentation and Final Commit

Files:

- Update `README.md`
- Add `docs/launcher-evaluation.md`

Work:

1. Document install, normal device-only use, status, upgrade, recovery, and
   uninstall commands.
2. Record the exact target profile, versions, hashes, validation evidence, and
   residual risks.
3. Run `git diff --check`, review the final tracked file set, and commit the
   implementation without build outputs or device data.
