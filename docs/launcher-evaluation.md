# Falcon-to-LVGL Launcher Evaluation

Date: 2026-08-05

Target: `OVERHEAD_Y01_SKU_CHN_PRO`, firmware 4.8.6, `RK3562_Orange_V0`,
AArch64 with glibc 2.36, Falcon logical screen 960x266 at direction 270.

## Decision

**GO** for using the Falcon mini-app as a device-side entry point to this
standalone LVGL application on the exact validated profile. Normal operation no
longer requires ADB: Falcon retains the desktop and boot experience, while LVGL
owns display and touch only for the duration of the launched application.

This is not a general launcher for other Youdao models or firmware. It also
does not prove integrations with OCR, scanning, dictionaries, accounts, OTA,
audio, or the Youdao input method.

## Installed Layout

| Purpose | Location |
| --- | --- |
| Falcon AppID | `8080992608050001` |
| Falcon name | `LVGL 应用` |
| Versioned native payload | `/userdisk/apps/lvgl-poc/releases/0.1.0` |
| Active release | `/userdisk/apps/lvgl-poc/current` |
| Runtime state and lock | `/run/lvgl-poc` |
| Bounded supervisor log | `/userdata/applog/lvgl-poc/supervisor.log` |

The Falcon package contains the page bytecode, app icon, and
`libjsapi_lvgl_launcher.so`. The native module accepts no shell text or caller
paths; it can only probe, start, and read status for the fixed payload.

## Real-Device Results

| Check | Result | Evidence |
| --- | --- | --- |
| Fresh install | Pass | `miniapp_cli install` returned `ret: 0`; host installer returned `INSTALL_PASS`. |
| Uninstall/reinstall | Pass | AppID package and the three dedicated directories were removed, Falcon remained ready, and reinstall returned `INSTALL_PASS`. |
| Falcon entry | Pass | Explicit `miniapp_cli start 8080992608050001 index` loaded the launcher and started the native supervisor. Normal device use is the same installed icon entry. |
| Duplicate entry | Pass | Two start requests 500 ms apart added one supervisor start record and produced one LVGL process. The supervisor uses an atomic directory lock. |
| Missing payload | Pass | With only the `current` link temporarily unavailable, the Falcon page displayed `LVGL release is not installed`; Falcon remained active and no supervisor or LVGL process started. The link was restored unchanged. |
| Normal exit | Pass | Two result-0 LVGL exits restored `guardian_run`, `runDictPen`, and `/usr/bin/miniapp`; no LVGL or supervisor process remained. |
| Unlimited interactive run | Pass | One icon-entry session remained active through 323 seconds. At 311 seconds RSS was 8,280 KB; the previous 300-second auto-exit did not fire. |
| LVGL crash | Pass | `SIGKILL` of the exact LVGL PID produced result 137 and restored Falcon in about three seconds. |
| Supervisor signal | Pass | `TERM` of the exact supervisor PID stopped LVGL, produced result 143, and restored Falcon in about two seconds. |
| Process isolation | Pass | Only the exact Falcon UI chain was stopped. Other guardian services were not targeted and no global `killall` is used. |
| Local tests | Pass | Supervisor fixtures, POSIX shell syntax, and all PowerShell parser checks passed. |
| ABI | Pass | Both artifacts are ELF64 AArch64 with no RPATH/RUNPATH. The LVGL executable requires at most GLIBC 2.34 on a glibc 2.36 device. |

## Current Artifacts

- LVGL executable SHA-256:
  `fb44beabecdf82f9879efb6d8f86ec2a766767b7e783b8666799188213b8e540`
- Native JSAPI SHA-256:
  `d840343cbd0a064f09bf13fdac4ce6b78895055b06c8ac06ce6585ad6b5d7840`
- Production AMR SHA-256:
  `4fd3cf4c290dd704dd9d89eb699390c6b97db1ee37e10a667f5e4ffc61c3d315`

## Recovery Model

The supervisor owns the handoff transaction. It validates the release and
hash, atomically acquires `/run/lvgl-poc/lock`, stops only the profiled Falcon
UI processes, launches LVGL, waits for it, and restores Falcon on every exit
path. Falcon restoration is retried after 1, 2, and 5 seconds. A device reboot
does not depend on this state because no boot-time launcher was installed.

## Residual Risks

- The profile is intentionally fixed to one PCBA, firmware, ABI, and screen
  configuration. An installer refusal after a firmware update is expected.
- Power loss during the short handoff was not destructively tested. The next
  boot should use Falcon because the design installs no boot hook, but a formal
  production qualification should test abrupt power removal and storage
  integrity.
- The launcher icon resource and installed manifest were verified. The captured
  Falcon home page was on a different carousel position, so final icon placement
  should be checked visually on the device after firmware UI changes.
- LVGL remains responsible for its own service adapters and application state.
  Visual complexity is feasible; proprietary device-service integration is the
  larger development risk.
