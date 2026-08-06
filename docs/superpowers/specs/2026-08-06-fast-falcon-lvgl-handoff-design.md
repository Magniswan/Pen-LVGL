# Faster Falcon-to-LVGL Handoff

## Objective

Reduce the perceived launch and exit latency of the existing Falcon mini-app
launcher without changing the ownership model: Falcon owns the display and
touch devices while idle, and the standalone LVGL process owns them only while
LVGL is active.

## Scope

The change is limited to `launcher/device/lvgl-supervisor.sh` and its local
tests. It will remove fixed one-second sleeps that occur before and after the
Falcon handoff, and replace coarse one-second Falcon restoration polling with a
short bounded polling interval. Existing process identity checks, lock
behavior, retry policy, and recovery on abnormal LVGL exit remain intact.

## Behavior

1. Acquire the existing lock and validate the LVGL release as today.
2. Stop only the exact Falcon guardian, `runDictPen`, and `/usr/bin/miniapp`
   processes.
3. Continue only after the targeted processes have exited or the existing
   bounded stop timeout reports failure. No unconditional delay is added after
   the stop operation.
4. Launch LVGL immediately after the handoff is confirmed.
5. On LVGL exit, start Falcon restoration as today, but check readiness at a
   short interval rather than sleeping for a full second between checks. The
   restoration still has a finite timeout and the existing 1/2/5-second retry
   schedule if an attempt fails.

The test mode keeps deterministic fixture behavior and does not introduce
real-device sleeps. Log and status formats are unchanged.

## Safety and Failure Handling

- Exact `/proc` command-line and executable matching remains mandatory.
- The supervisor must never use a global process kill.
- If a targeted Falcon process does not exit within its current timeout, LVGL
  is not started and the existing restoration path is used.
- If Falcon does not become ready within the restoration timeout, the existing
  error state and retry behavior remain in force.
- No resident daemon, IPC protocol, compositor, or simultaneous Falcon/LVGL
  rendering is introduced by this change.

## Verification

- Keep `sh -n` and PowerShell parser checks passing.
- Extend supervisor fixtures to assert that the faster polling path still
  restores Falcon after normal, failed, and signaled LVGL exits.
- Run `scripts/tests/test_supervisor.ps1` and confirm `supervisor_tests=PASS`.
- On-device measurement is outside the host-only change; the launcher log
  timestamps will be used to compare handoff and restoration latency.

## Non-Goals

- Embedding LVGL inside the Vue/QuickJS mini-app process.
- Running Falcon and LVGL concurrently on the same DRM CRTC or touch device.
- Changing LVGL rendering, input handling, package layout, or boot behavior.
