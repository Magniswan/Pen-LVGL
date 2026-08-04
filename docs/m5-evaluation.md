# M5 Real-Device Evaluation

Date: 2026-08-04

Target: `OVERHEAD_Y01_SKU_CHN_PRO`, firmware 4.8.6, RK3562/AArch64,
Buildroot Linux 5.10.160, glibc 2.36, 480x960 DSI panel.

## Decision

**GO** for continuing LVGL as a native UI platform on this exact device
profile. Display, touch, interaction, visual quality, sustained performance,
memory use, and Falcon recovery are all viable. This is not yet a GO for
replacing Falcon in a shippable product because proprietary dictionary-pen
services remain a separate integration gate.

## Validation Matrix

| Area | Result | Evidence |
| --- | --- | --- |
| AArch64 ABI and target libraries | Pass | ELF64 little-endian AArch64; interpreter `/lib/ld-linux-aarch64.so.1`; only `libdrm`, C/C++ runtime, math, and libc are dynamic dependencies. |
| DRM display and orientation | Pass | Connected `DSI-1`, 480x960 mode, double dumb buffers, page flips, and the 960x266 rotated logical canvas rendered correctly. |
| Touch mapping and controls | Pass | `hyn_ts` discovery plus 73 automated input updates exercised navigation, tap, long press, slider, scrolling, and drag paths. |
| Visual features | Pass | Chinese subset glyphs, English text, PNG alpha, opacity, rounded corners, gradients, and local animation rendered correctly in `test-results/visual/visual-final.png`. |
| Animation performance | Pass | The final 300.013-second run produced 12,185 frames (40.6 FPS overall), 14.97 ms average render/submit time, and 29.80 ms peak. The captured frame showed 37.7 FPS. Acceptance floor is 30 FPS. |
| Memory | Pass | RSS stayed at 8,276 KB for all 300 recorded samples, well below the 40 MB limit. |
| CPU | Acceptable with optimization work | Continuous software rendering averaged 43.1% of one CPU core across the final run, with a 62.7% peak. This is moderate, not negligible, and should be reduced by stopping animations off-screen and lowering idle refresh work. |
| Fault and timeout recovery | Pass | Injected exit code 42 and a wrapper timeout both restored the Falcon guardian and `/usr/bin/miniapp`. |
| Repeated handoff | Pass | Five consecutive Falcon -> LVGL -> Falcon cycles completed, each with a stable restored miniapp. |
| Five-minute continuous run | Pass | `scripts/run_m5.ps1` completed with `runtime_s=300.013`, `result=0`, 12,185 frames, stable 8,276 KB RSS, a valid PPM capture, and restored Falcon guardian, `runDictPen`, and `/usr/bin/miniapp` processes. |
| Persistent mutation | Pass | PoC executable, image, wrapper, PID, and logs use `/tmp`; no boot, partition, installed mini-app, or user-data change was introduced. |

## Performance Impact

The memory impact is small. The meaningful cost is CPU and display bandwidth:
the current implementation continuously animates and performs software LVGL
rendering, rotation, dirty-region copy, and KMS page flips. Under that workload
it uses a fraction of one RK3562 core and remains above 30 FPS. Static or mostly
idle application pages should consume materially less CPU once the event loop
uses stricter invalidation and animation suspension.

PNG cold decode can cause a short latency/CPU spike. Decoded images should be
cached or converted to an LVGL-native asset for production. Full-screen effects
and multiple simultaneous animations should be profiled on the actual page,
not assumed from the small PoC animation.

## Development and UI Assessment

LVGL is suitable for polished embedded interfaces. This PoC demonstrates clean
typography hierarchy, responsive controls, alpha, gradients, rounded geometry,
images, and animation without Qt, Flutter, a browser, or a GPU stack. More
complex UI such as virtualized lists, charts, custom keyboards, modal flows,
timelines, and multi-page state machines is feasible.

Long-term development is more explicit than Falcon/Vue: layout, lifecycle,
threads, storage, networking, input methods, and native service integration are
C/C++ responsibilities. Maintainability is good only if the platform boundary
remains separate from page code and reusable services own cancellation,
logging, and cleanup. LVGL itself is mature; the device adaptation layer is the
part this project must own.

Hardware-rich functions are possible but not proven by this PoC. Scanning,
OCR, dictionary databases, audio, Wi-Fi, OTA, account state, and the Youdao
input method require documented system APIs or carefully isolated native
adapters. Video may use a separate GStreamer/KMS plane after per-firmware plane
and composition testing. These integrations, rather than visual complexity,
are the primary schedule risk.

## Conditions for Product Development

1. Replace the temporary 107-glyph font generated from a workstation font with an explicitly redistributable Noto Sans CJK or Source Han subset, and record its license.
2. Turn the device profile into a versioned input and add host tests for coordinate boundaries, DRM selection, metrics, and guardian selection.
3. Add an idle policy, animation suspension, decoded-image caching, and page-level CPU budgets.
4. Define a supported bridge for each required proprietary service before committing to a full application rewrite.
5. Keep recovery device-side and temporary until repeated power-loss, crash, and upgrade testing establishes a production launcher design.

## Current Artifact

- LVGL: 9.5.0
- Binary: `build/m5/lvgl_poc`
- Size: 1,282,672 bytes
- SHA-256: `3fdae21f0197e3771c41e36a94d6d1788e00deb8a1e80fc7f2b9de9730a9d8cd`
- M5 log: `test-results/m5/five-minute-wrapper.log`
- M5 capture SHA-256: `f9b2e07b63d06aa1d8bed088a7fd6d7705c657d10e217e7ac6618440893f43b4`
- No embedded build-host RPATH
