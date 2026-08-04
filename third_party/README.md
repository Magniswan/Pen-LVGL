# Vendored Dependencies

## LVGL

- Version: 9.5.0
- Upstream: `https://github.com/lvgl/lvgl/tree/v9.5.0`
- License: MIT, see `lvgl/LICENCE.txt` and `lvgl/COPYRIGHTS.md`
- Included content: root build metadata, `src/`, and the CMake environment helper

Local change: `src/libs/lodepng/lv_lodepng.c` includes the numeric LodePNG
error code and error text in decoder warnings. This was added while diagnosing
the PoC PNG memory-pool failure.

## libdrm Headers

- Version: 2.4.123
- Upstream: `https://gitlab.freedesktop.org/mesa/drm/-/tree/libdrm-2.4.123`
- License: MIT, as declared in `libdrm/meson.build` and in the retained headers
- Included content: `xf86drm.h`, `xf86drmMode.h`, and `include/drm/`

Only headers are vendored. The target executable dynamically resolves the
firmware's `libdrm.so.2`; no libdrm implementation is built into the PoC.
