#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TOOLCHAIN_ROOT="${TOOLCHAIN_ROOT:-/opt/arm-gnu-toolchain-11.3.rel1-x86_64-aarch64-none-linux-gnu}"
CXX="$TOOLCHAIN_ROOT/bin/aarch64-none-linux-gnu-g++"
READELF="$TOOLCHAIN_ROOT/bin/aarch64-none-linux-gnu-readelf"
NM="$TOOLCHAIN_ROOT/bin/aarch64-none-linux-gnu-nm"
OUTPUT="$ROOT/libs/arm64-orange/libjsapi_lvgl_launcher.so"

test -x "$CXX" || { echo "missing compiler: $CXX" >&2; exit 2; }
mkdir -p "$(dirname "$OUTPUT")"

mapfile -t SDK_SOURCES < <(find "$ROOT/native/iot-miniapp-sdk/src" -name '*.cpp' -print | sort)
mapfile -t PLUGIN_SOURCES < <(find "$ROOT/native/src" -name '*.cpp' -print | sort)

"$CXX" -std=c++17 -O3 -fPIC -shared -w \
  -I"$ROOT/native/iot-miniapp-sdk/include" \
  -I"$ROOT/native/src" \
  "${SDK_SOURCES[@]}" "${PLUGIN_SOURCES[@]}" \
  -pthread -Wl,--unresolved-symbols=ignore-all \
  -Wl,-soname,libjsapi_lvgl_launcher.so \
  -o "$OUTPUT"

file "$OUTPUT"
"$READELF" -h "$OUTPUT" | grep -E 'Class:|Machine:'
"$READELF" -d "$OUTPUT" | grep -E 'NEEDED|RPATH|RUNPATH' || true
"$NM" -D "$OUTPUT" | grep custom_init_jsapis
