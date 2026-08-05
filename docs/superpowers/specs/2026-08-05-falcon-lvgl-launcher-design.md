# Falcon Entry Point for the Standalone LVGL App

## Status

Approved design for the Y01 profile. Implementation is intentionally separate
from this document and must begin only after the spec review is complete.

## Objective

Make the standalone LVGL executable usable from the normal Falcon experience:
the user taps an LVGL icon in Falcon, the device switches to the independent
LVGL app, and the user can return to Falcon without ADB. Falcon remains
installed and its system services remain available.

This is a launcher and packaging project, not a request to make LVGL run as a
Vue component. LVGL remains a native AArch64 process with its own render loop,
DRM backend, touch backend, resources, and lifecycle.

## Target Profile

The first release is limited to the profiled device:

- Profile: `OVERHEAD_Y01_SKU_CHN_PRO`
- Firmware: `4.8.6`
- SoC/ABI: RK3562, AArch64, glibc 2.36
- Falcon logical canvas: 960x266, physical panel 480x960, direction 270
- Display: `/dev/dri/card0`, connected `DSI-1`
- Touch: device identified by evdev name `hyn_ts`
- Persistent executable storage: `/userdisk`
- Persistent log storage: `/userdata/applog`

No value above is a cross-device default. A future profile must provide its own
launcher payload, ABI, screen configuration, and verification record.

## Selected Approach

Use a thin Falcon launcher mini-app plus a detached one-shot supervisor.

```text
Falcon icon
    |
    v
launcher mini-app -- native JSAPI start()
    |
    v
detached supervisor
    |
    +-- stop only runDictPen guardian and its miniapp
    +-- start the independent LVGL executable
    +-- wait for exit, crash, or signal
    +-- restore Falcon guardian and miniapp
```

The supervisor is a separate process so stopping Falcon cannot terminate the
code responsible for restoring Falcon. A resident daemon is deliberately not
used in the first release: it would add a permanent process, IPC protocol, and
additional upgrade and security surface without improving the single-app
workflow.

## Components and Boundaries

### Falcon launcher mini-app

The launcher package supplies the icon, app name, a short launching state, and
an error/retry state. It calls one fixed native operation and does not open DRM,
read evdev, or execute shell text itself.

The development package uses the dedicated 16-digit AppID
`8080992608050001`. The installer refuses installation if that ID is already
owned by a different package. The ID is a package identity, not a device
profile default; production distribution must assign an approved ID before
release.

The launcher calls:

```text
start() -> Promise<{accepted, state, supervisorPid, message}>
```

The native module accepts no executable path, shell command, or arbitrary
arguments from JavaScript. It starts only the supervisor at the fixed current
release path and returns an already-running result when the lock exists.

### Native launcher module

The module is `libjsapi_lvgl_launcher.so` and follows the target Falcon native
JSAPI loader contract. Its synchronous work is limited to capability and state
checks. Process creation is asynchronous and uses a fixed `fork`/`setsid`/
`exec` path with stdout and stderr redirected to the supervisor log.

The module validates that the target profile matches the package, the current
release manifest exists, the executable is regular and executable, and its
recorded SHA-256 matches before starting the supervisor. It translates native
errors into stable JS errors and never logs credentials or arbitrary command
text.

### Device supervisor

The supervisor is installed at:

```text
/userdisk/apps/lvgl-poc/current/bin/lvgl-supervisor.sh
```

It uses an atomic `mkdir` lock under `/run/lvgl-poc/lock`, records a bounded
state file, and owns the complete Falcon handoff. It matches processes by exact
`/proc/<pid>/cmdline` identity:

- `/usr/bin/guardian_run /usr/bin/runDictPen`
- the corresponding `/usr/bin/runDictPen` shell process
- `/usr/bin/miniapp`

It must never call global `killall guardian_run`, because other guardian
instances supervise audio, Wi-Fi, Bluetooth, OTA, and input services.

### Independent LVGL release

The release is installed under a versioned directory and activated by an
atomic `current` link or directory replacement:

```text
/userdisk/apps/lvgl-poc/
  releases/<version>/
    bin/lvgl_poc
    bin/lvgl-supervisor.sh
    assets/poc_badge.png
    assets/fonts/...
    manifest.json
  current/
  state/
```

The executable runs with `POC_RUN_SECONDS=0`, where zero means no automatic
timeout. The existing top-right LVGL exit action remains the normal exit path.

Logs and state use bounded files under:

```text
/userdata/applog/lvgl-poc/
```

The supervisor keeps three rotated logs of at most 256 KiB each and removes
stale PID/state files only after verifying that no matching process is alive.

## Lifecycle State Machine

```text
IDLE
  -> LAUNCHING       icon tap accepted, lock acquired
  -> LVGL_RUNNING    Falcon handoff succeeded and LVGL started
  -> RESTORING       LVGL exited, crashed, or received a signal
  -> IDLE            guardian and miniapp verified alive

LAUNCHING -> ERROR   preflight, lock, handoff, or exec failure
RESTORING -> RETRY   Falcon verification failed
RETRY -> IDLE        bounded retry succeeds
RETRY -> ERROR       bounded retry exhausted
```

The supervisor follows this sequence:

1. Acquire the lock and write `launching` state.
2. Discover and record the exact Falcon process identities.
3. Stop the DictPen guardian, its `runDictPen` child, and the miniapp only.
4. Wait for DRM and touch ownership to be released.
5. Start LVGL and write `running` state with its PID.
6. Wait for normal exit, nonzero exit, `TERM`, `INT`, or `HUP`.
7. Stop LVGL if needed, restore the Falcon guardian, and wait for miniapp.
8. Retry Falcon restoration at bounded delays of 1, 2, and 5 seconds.
9. Write `idle` or `error`, remove the lock, and exit.

The launcher prevents duplicate starts. A supervisor crash or a power loss
cannot configure LVGL as a boot service; the next boot follows the unchanged
firmware path into Falcon. A forced `SIGKILL` during the handoff is recorded as
a residual-risk test case rather than silently treated as a successful exit.

## User Experience

- Falcon shows an icon named `LVGL 应用` using a dedicated package icon.
- Tap opens a short `正在启动` state and then hands the display to LVGL.
- A preflight or exec error keeps Falcon visible and offers retry or return.
- LVGL's existing exit button requires confirmation and returns to Falcon.
- Repeated taps while launching produce one `already_running` result.
- No boot-time LVGL autostart is enabled.

## Installation and Upgrade

Host scripts provide the portable one-time setup:

```text
scripts/install_device_app.ps1
scripts/status_device_app.ps1
scripts/uninstall_device_app.ps1
```

`install_device_app.ps1` requires exactly one connected device and performs:

1. Read-only profile, firmware, ABI, free-space, and AppID checks.
2. SHA-256 verification of every ELF, script, resource, and launcher package.
3. Staging under `/userdisk/apps/lvgl-poc/releases/<version>.staging`.
4. Executable mode and manifest validation on the device.
5. Atomic activation of the new `current` release while retaining one rollback
   release.
6. `miniapp_cli install` of the launcher `.amr` and a real icon-start smoke
   test.

The installer never writes `/etc/init.d`, `/etc/inittab`, the EROFS root
filesystem, or the existing `skip_login.sh` hook. Uninstall first removes the
launcher through `miniapp_cli uninstall`, then removes only the app's
`/userdisk/apps/lvgl-poc` and `/userdata/applog/lvgl-poc` paths after verifying
that no supervisor or LVGL process remains.

## Error and Recovery Contract

- Profile mismatch: fail before stopping Falcon.
- Missing or mismatched release payload: fail before stopping Falcon.
- Falcon process not found: fail without changing display state.
- Falcon handoff timeout: restore any process already stopped and report the
  exact state.
- LVGL nonzero exit or signal: always enter restoration.
- Falcon restoration failure: retry three times, retain logs/state, and leave
  the device accessible through ADB without rebooting it.
- Normal reboot: Falcon starts normally because no persistent boot hook is
  installed.

## Verification Plan

### Host and package checks

- PowerShell script syntax and shell `sh -n` checks.
- AArch64 ELF, interpreter, GLIBC/GLIBCXX requirements, `DT_NEEDED`, and no
  host RPATH/RUNPATH.
- Launcher package manifest, icon, AppID collision, and resource checks.
- Staging/activation rollback and uninstall path tests.

### Real-device checks

1. Install from a clean Falcon state and verify the icon appears.
2. Enter LVGL, use interaction and visual pages, exit, and verify Falcon.
3. Repeat the enter/exit cycle twice and measure residual processes/memory.
4. Tap the icon repeatedly during launch and verify one supervisor only.
5. Inject LVGL failure and verify Falcon restoration.
6. Exercise supervisor `TERM`, `INT`, and `HUP` cleanup.
7. Reboot from Falcon and from LVGL; both boots must return to Falcon.
8. Upgrade to a second release, force activation failure, and verify rollback.
9. Uninstall and verify the original Falcon process tree and data remain.

Acceptance requires no unrelated guardian process to be terminated, no stale
LVGL process after exit, successful Falcon restoration after normal and
abnormal exits, and no change to boot configuration or system partitions.

## Out of Scope

- Falcon and LVGL rendering simultaneously on one frame.
- Replacing Falcon system services or proprietary dictionary APIs.
- LVGL boot autostart.
- Support for unprofiled dictionary-pen models or firmware versions.
- Production signing or distribution AppID approval.
