#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TOOLCHAIN_ROOT="${TOOLCHAIN_ROOT:-/opt/arm-gnu-toolchain-11.3.rel1-x86_64-aarch64-none-linux-gnu}"
CXX="$TOOLCHAIN_ROOT/bin/aarch64-none-linux-gnu-g++"
READELF="$TOOLCHAIN_ROOT/bin/aarch64-none-linux-gnu-readelf"
NM="$TOOLCHAIN_ROOT/bin/aarch64-none-linux-gnu-nm"
OUTPUT="$ROOT/libs/arm64-orange/libjsapi_lvgl_launcher.so"
BUILD_DIR="$(mktemp -d)"
trap 'rm -rf -- "$BUILD_DIR"' EXIT

test -x "$CXX" || { echo "missing compiler: $CXX" >&2; exit 2; }
mkdir -p "$(dirname "$OUTPUT")"

mapfile -t SDK_SOURCES < <(find "$ROOT/native/iot-miniapp-sdk/src" -name '*.cpp' -print | sort)
mapfile -t PLUGIN_SOURCES < <(find "$ROOT/native/src" -name '*.cpp' -print | sort)
OBJECTS=()
index=0
for source in "${SDK_SOURCES[@]}"; do
  object="$BUILD_DIR/sdk-$index.o"
  "$CXX" -std=gnu++17 -O3 -fPIC -w \
    -I"$ROOT/native/iot-miniapp-sdk/include" \
    -c "$source" -o "$object"
  OBJECTS+=("$object")
  index=$((index + 1))
done
for source in "${PLUGIN_SOURCES[@]}"; do
  object="$BUILD_DIR/plugin-$index.o"
  strict=(-Wall -Wextra -Werror -Wno-unused-parameter -Wno-cast-function-type)
  if [[ "$source" == */Launcher/Launcher.cpp ]]; then
    strict+=(-Wpedantic)
  fi
  "$CXX" -std=gnu++17 -O3 -fPIC "${strict[@]}" \
    -isystem "$ROOT/native/iot-miniapp-sdk/include" \
    -I"$ROOT/native/src" \
    -c "$source" -o "$object"
  OBJECTS+=("$object")
  index=$((index + 1))
done

"$CXX" -shared "${OBJECTS[@]}" \
  -pthread -Wl,--unresolved-symbols=ignore-all \
  -Wl,-soname,libjsapi_lvgl_launcher.so \
  -o "$OUTPUT"

file "$OUTPUT"
"$READELF" -h "$OUTPUT" | grep -E 'Class:|Machine:'
"$READELF" -d "$OUTPUT" | grep -E 'NEEDED|RPATH|RUNPATH' || true
"$NM" -D "$OUTPUT" | grep custom_init_jsapis
