# LVGL Multi-App Platform and Focus Timer

## Status

Approved product and architecture design for the validated Y01 device profile.
Implementation must preserve the existing Falcon recovery guarantees and must
not add a boot-time service.

## Objective

Evolve the current standalone LVGL proof of concept into a small, repeatable
application platform with:

- one native LVGL application launcher;
- the existing LVGL interaction and diagnostics application;
- one new single-page focus timer application;
- a shared pull-down status panel available in every in-house LVGL app; and
- development rules that make later applications consistent and isolated.

The Falcon mini-app remains the single device entry point. Falcon hands display
and touch ownership to the native LVGL session and is restored only when the
whole LVGL session ends.

## Target and Scope

The first release remains limited to the already validated profile:

- device profile: `OVERHEAD_Y01_SKU_CHN_PRO`;
- firmware: `4.8.6`;
- architecture: AArch64 with glibc 2.36;
- logical LVGL canvas: 960 x 266;
- display: `/dev/dri/card0`, connected `DSI-1`;
- touch device: evdev name `hyn_ts`; and
- LVGL version: 9.5.0.

The shared status panel is required for all applications built in this
repository. It is not intended to cover Falcon, third-party programs, or a
process that does not link the shared application shell.

## Selected Architecture

Use separate application processes behind a native session manager:

```text
Falcon icon
    |
    v
existing Falcon supervisor
    |
    v
lvgl_session
    |
    +-- lvgl_launcher
    +-- lvgl_poc
    +-- focus_timer
```

The existing Falcon supervisor retains responsibility for the outer handoff:
validate the release, stop only the Falcon UI process chain, start the native
session, wait for it, and restore Falcon on every exit path. It must not become
an application router.

`lvgl_session` owns the inner application lifecycle. It launches exactly one
registered child application at a time, receives a typed transition command,
waits for the child to release DRM and input, and then starts the requested
application. Ending the session is the only inner action that returns control
to the Falcon supervisor.

This preserves process isolation without building a compositor. A compositor
is out of scope because it would require permanent DRM ownership, application
surface transport, input routing, IPC, and a substantially larger recovery
model.

## Components and Boundaries

### Session manager

`lvgl_session` is a small C++ process with no LVGL, DRM, or evdev dependency.
It reads a compiled, fixed application registry and starts child executables by
registered ID. It never accepts an executable path or shell command from a
child, environment variable, or Falcon JavaScript.

The session and its child use an inherited Unix `socketpair`. Messages use a
versioned fixed structure containing only a command enum and an application
ID. Supported commands are:

- launch a registered application;
- return to the LVGL launcher; and
- end the LVGL session and return to Falcon.

Malformed messages, unknown IDs, duplicate commands, and commands from a child
that has already exited are rejected. The socket is private to the session and
the active child; no persistent control socket is created.

If a non-launcher app crashes or exits without a valid command, the session
starts the launcher and reports one structured error for display. If the
launcher fails three consecutive times, the session exits so the outer
supervisor can restore Falcon.

### Application registry

Every application has one stable `AppDescriptor` with:

- a compile-time numeric ID;
- a stable string ID;
- display name;
- icon resource;
- executable target name; and
- application version.

The launcher and session consume the same registry definition. Adding an app
requires adding its descriptor, CMake target, resources, and tests. The device
release manifest still records executable and resource hashes, but it does not
override the compiled registry.

### Platform runtime

`platform_runtime` owns the shared device setup currently embedded in
`src/lvgl_poc.cpp`:

- signal handling;
- DRM open, LVGL display creation, and flush handling;
- evdev open and LVGL input registration;
- LVGL tick and main-loop scheduling;
- optional capture and runtime metrics hooks; and
- ordered timer, input, display, and LVGL cleanup.

Applications provide their UI creation callback and receive a small runtime
context. Applications must not open DRM or evdev directly and must not copy the
platform entry-point code.

### Shared application shell

`app_shell` is mandatory for every in-house LVGL application. It creates the
system UI on `lv_layer_top()` after the application content exists. It owns:

- the top-edge gesture recognizer;
- the pull-down panel and scrim;
- panel animations and gesture arbitration;
- time, battery, and current-application presentation;
- the Home command; and
- the confirmed Exit to Falcon command.

Applications may provide declarative shell metadata such as the current title.
They may not replace the panel hierarchy, gesture thresholds, system actions,
or visual tokens.

Battery or time provider failure must not block the application. Missing values
are rendered as unavailable and retried at a bounded interval.

### Native LVGL launcher

`lvgl_launcher` is the first child started by the session. It uses a horizontal
snap pager from the first release, even when all current applications fit on
one page. The current two applications occupy the first page. Page indicators
and swipe handling are present so later pages require only registry entries,
not a layout rewrite.

The launcher shows the product identity and time on the left and application
entries in the paged area. Each entry has an icon, a short name, and a compact
status line. Selecting an application sends a typed launch command and shows a
transition state until the session replaces the process. It does not ask for a
second confirmation.

### Existing LVGL proof of concept

The current interaction, visual, and diagnostics UI becomes the registered
`lvgl_poc` application. Its device setup moves to `platform_runtime`, and it
adopts `app_shell`. Its business screens remain otherwise focused on hardware
and visual validation.

Existing mojibake in user-facing Chinese text must be corrected as part of the
migration. Source files, generated font inputs, and resource metadata use
UTF-8 consistently.

### Focus timer

`focus_timer` is a single-page app. It deliberately excludes tasks, accounts,
history, analytics, persistence, and background execution.

The left side shows a large remaining-time value and progress arc. The right
side provides 15, 25, and 45 minute presets plus start/pause and reset controls.
Preset changes are disabled while the timer is running. Completion replaces
the running state with a clear completed state and one action to start the same
duration again.

The timer derives remaining time from a monotonic deadline. A UI timer updates
the presentation from that deadline; it never treats periodic callback count
as elapsed time. Opening the shared status panel does not pause the timer.
Returning to the launcher ends the session instance and discards timer state.

## Shared Status Panel Interaction

The top 18 logical pixels are a transparent gesture start region. A gesture is
claimed only after it moves primarily downward and crosses a 24 pixel vertical
threshold. Before the claim, normal application input remains available. Once
claimed, underlying application widgets receive no further events from that
gesture.

The panel is approximately 82 pixels high and follows the pointer while
dragging. On release it opens when its position or release velocity passes the
shared threshold; otherwise it returns to the closed position. The settle
animation lasts 180 to 220 ms. Tapping the scrim or swiping upward closes it.

The panel shows time, battery state, current application, a Home icon button,
and an Exit to Falcon icon button. Exit to Falcon requires confirmation. Home
does not require confirmation and is hidden or disabled when already in the
launcher.

## Visual and Interaction Rules

- Design for the fixed 960 x 266 logical canvas; centralize these profile
  dimensions rather than scattering literals through application code.
- Primary touch targets are at least 44 x 44 logical pixels.
- Use the shared type scale, spacing, colors, button states, icons, and focus
  behavior from `app_shell` and the common theme module.
- Application content must not overlap the top-edge gesture region with a
  control that depends on a downward drag.
- Horizontal launcher pagination uses axis locking so a mostly vertical gesture
  can still open the system panel.
- Dynamic text must remain inside its allocated bounds. Long app names are
  truncated according to one shared rule and are never allowed to resize tiles.
- Timers and animations must be destroyed before their owning object or app
  context is released.

## Source and Build Layout

The implementation should converge on these ownership boundaries:

```text
src/
  runtime/          LVGL/device runtime and shared main-loop support
  shell/            common theme, status panel, and system actions
  session/          process registry, socket protocol, and child lifecycle
  platform/         existing DRM, input, and device-profile backends
apps/
  launcher/         paged LVGL application launcher
  poc/              migrated interaction and diagnostics app
  focus_timer/      single-page focus timer
```

All targets use C++17 and retain `-Wall -Wextra -Wpedantic -Werror`. LVGL calls
are confined to the UI thread. Background work communicates through bounded
messages or snapshots and never mutates LVGL objects directly. Event callbacks
should translate input into controller actions rather than contain application
state machines.

Build outputs, generated packages, device logs, and transient captures are not
tracked. Fonts and icons must include their source, license or provenance,
dimensions, and reproducible generation command when generated.

## Lifecycle and Error Handling

The normal lifecycle is:

```text
Falcon -> supervisor -> session -> launcher -> selected app -> launcher
                                                    |
                                                    +-> end session -> Falcon
```

Required behavior:

- a child must release LVGL, evdev, and DRM before its process exits;
- the session waits for child exit before starting the next application;
- an unknown or unauthorized app ID never starts a process;
- child launch failure returns to the launcher with a bounded error;
- a crashed app returns to the launcher instead of ending the whole session;
- repeated launcher failure ends the session and restores Falcon;
- termination of the session terminates its exact active child before exiting;
- termination of the outer supervisor retains the existing idempotent Falcon
  restoration path; and
- no session or app is registered as a boot service.

The last application frame may remain visible during the short process handoff.
Applications display a shared transition treatment before requesting a switch.
The first implementation does not add a separate compositor or cross-fade
between process surfaces.

## Verification

### Host tests

- session protocol encode/decode and version rejection;
- registry lookup and unknown-ID rejection;
- child normal exit, crash, duplicate command, and launch failure;
- launcher horizontal snap and page indicator state;
- status panel gesture direction, threshold, open/close, and event capture;
- focus timer start, pause, reset, completion, and delayed UI callback accuracy;
- UTF-8 source and generated-font coverage for all displayed Chinese strings;
- shell syntax, PowerShell parser checks, and package manifest checks; and
- AArch64 ELF, dependency, interpreter, and RPATH/RUNPATH checks.

### Device tests

1. Enter the native LVGL launcher from the installed Falcon icon.
2. Verify horizontal paging, page indicators, and application selection.
3. Pull down and dismiss the shared panel from every launcher and app screen.
4. Verify horizontal pager gestures do not suppress intentional panel gestures.
5. Run each focus preset, including a full completion and restart flow.
6. Switch among apps at least 20 times and check first-frame latency.
7. Kill each app process and verify a return to the launcher with no stale DRM
   or touch owner.
8. Terminate the session and supervisor separately and verify Falcon recovery.
9. Confirm no residual child process and no unbounded RSS growth.

Target budgets are a typical interaction frame below 20 ms, per-application
RSS no greater than 16 MiB, and a visible first frame within 500 ms of an app
selection. Final acceptance uses recorded measurements on the validated device.

## Out of Scope

- coverage of Falcon or third-party programs by the shared status panel;
- an LVGL compositor or simultaneous rendering by multiple apps;
- persistent timer history or resuming a timer after leaving the app;
- background notifications, proprietary dictionary services, or account data;
- unprofiled dictionary-pen models or firmware; and
- replacing Falcon at boot.
