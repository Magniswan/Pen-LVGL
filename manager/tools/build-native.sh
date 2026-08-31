#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
REPOSITORY="$(cd "$ROOT/.." && pwd)"
TOOLCHAIN_ROOT="${TOOLCHAIN_ROOT:-/opt/arm-gnu-toolchain-11.3.rel1-x86_64-aarch64-none-linux-gnu}"
CXX="$TOOLCHAIN_ROOT/bin/aarch64-none-linux-gnu-g++"
READELF="$TOOLCHAIN_ROOT/bin/aarch64-none-linux-gnu-readelf"
NM="$TOOLCHAIN_ROOT/bin/aarch64-none-linux-gnu-nm"
OUTPUT="$ROOT/libs/arm64-orange/libjsapi_lvgl_manager.so"
BUILD_DIR="$(mktemp -d)"
trap 'rm -rf -- "$BUILD_DIR"' EXIT

test -x "$CXX" || { echo "missing compiler: $CXX" >&2; exit 2; }
mkdir -p "$(dirname "$OUTPUT")" "$BUILD_DIR/generated/lvgl_platform"

PRODUCTION="${MANAGER_PRODUCTION:-0}"
PUBLIC_KEY="${OFFICIAL_PUBLIC_KEY_HEX:-}"
PAYLOAD="${MANAGER_PLATFORM_PACKAGE:-}"
PAYLOAD_SOURCE="$ROOT/native/src/embedded_payload_unprovisioned.cpp"

if [[ "$PRODUCTION" == "1" ]]; then
  test -n "$PUBLIC_KEY" || { echo "production manager requires OFFICIAL_PUBLIC_KEY_HEX" >&2; exit 3; }
  test -n "$PAYLOAD" || { echo "production manager requires MANAGER_PLATFORM_PACKAGE" >&2; exit 3; }
fi

python3 - "$PRODUCTION" "$PUBLIC_KEY" "$PAYLOAD" "$BUILD_DIR/generated" <<'PY'
import os
import pathlib
import re
import stat
import sys

production, key, payload, output = sys.argv[1:]
output = pathlib.Path(output)
rfc_key = 'd75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a'
if key and (not re.fullmatch(r'[0-9a-f]{64}', key) or key == '0' * 64 or key == rfc_key):
    raise SystemExit('official public key is malformed or forbidden')
if production == '1' and (not key or not payload):
    raise SystemExit('production manager provisioning is incomplete')

(output / 'lvgl_platform' / 'official_key_config.h').write_text(
    '#pragma once\n#include <string_view>\nnamespace lvgl_platform {\n'
    f'inline constexpr std::string_view kOfficialReleasePublicKeyHex = "{key}";\n'
    '}  // namespace lvgl_platform\n', encoding='utf-8', newline='\n')
(output / 'manager_build_config.h').write_text(
    '#pragma once\n#include <string_view>\nnamespace manager_build {\n'
    'inline constexpr std::string_view kPlatformVersion = "1.0.0";\n'
    'inline constexpr std::string_view kSdkAbi = "1.0";\n'
    '}  // namespace manager_build\n', encoding='utf-8', newline='\n')

if payload:
    path = pathlib.Path(payload)
    details = path.lstat()
    if not stat.S_ISREG(details.st_mode) or details.st_size <= 0 or details.st_size > 64 * 1024 * 1024:
        raise SystemExit('embedded platform package must be a bounded regular file')
    data = path.read_bytes()
    lines = []
    for start in range(0, len(data), 16):
        lines.append('    ' + ', '.join(f'0x{value:02x}' for value in data[start:start + 16]) + ',')
    (output / 'embedded_platform_payload.cpp').write_text(
        '#include "manager_embedded_payload.h"\nnamespace manager_embedded {\n'
        'const std::uint8_t kPlatformPackage[] = {\n' + '\n'.join(lines) + '\n};\n'
        f'const std::size_t kPlatformPackageSize = {len(data)};\n'
        '}  // namespace manager_embedded\n', encoding='utf-8', newline='\n')
PY

if [[ -n "$PAYLOAD" ]]; then
  PAYLOAD_SOURCE="$BUILD_DIR/generated/embedded_platform_payload.cpp"
fi

mapfile -t SDK_SOURCES < <(find "$REPOSITORY/launcher/native/iot-miniapp-sdk/src" -name '*.cpp' -print | sort)
mapfile -t PLATFORM_SOURCES < <(find "$REPOSITORY/platform/src" -name '*.cpp' -print | sort)
mapfile -t MANAGER_SOURCES < <(find "$ROOT/native/src" -name '*.cpp' ! -name 'embedded_payload_unprovisioned.cpp' -print | sort)
MANAGER_SOURCES+=("$PAYLOAD_SOURCE")
OBJECTS=()
index=0

for source in "${SDK_SOURCES[@]}"; do
  object="$BUILD_DIR/sdk-$index.o"
  "$CXX" -std=gnu++17 -O3 -fPIC -w \
    -I"$REPOSITORY/launcher/native/iot-miniapp-sdk/include" \
    -c "$source" -o "$object"
  OBJECTS+=("$object")
  index=$((index + 1))
done

for source in "${PLATFORM_SOURCES[@]}"; do
  object="$BUILD_DIR/platform-$index.o"
  "$CXX" -std=gnu++17 -O3 -fPIC -Wall -Wextra -Wpedantic -Werror \
    -I"$BUILD_DIR/generated" -I"$REPOSITORY/platform/include" \
    -I"$REPOSITORY/third_party/lvgl/src/libs/thorvg" \
    -c "$source" -o "$object"
  OBJECTS+=("$object")
  index=$((index + 1))
done

for source in "${MANAGER_SOURCES[@]}"; do
  object="$BUILD_DIR/manager-$index.o"
  "$CXX" -std=gnu++17 -O3 -fPIC -Wall -Wextra -Werror \
    -Wno-unused-parameter -Wno-cast-function-type \
    -isystem "$REPOSITORY/launcher/native/iot-miniapp-sdk/include" \
    -I"$BUILD_DIR/generated" -I"$ROOT/native/include" \
    -I"$ROOT/native/src" -I"$REPOSITORY/platform/include" \
    -c "$source" -o "$object"
  OBJECTS+=("$object")
  index=$((index + 1))
done

"$CXX" -shared "${OBJECTS[@]}" -pthread -ldl \
  -Wl,--unresolved-symbols=ignore-all \
  -Wl,-soname,libjsapi_lvgl_manager.so \
  -o "$OUTPUT"

file "$OUTPUT"
"$READELF" -h "$OUTPUT" | grep -E 'Class:|Machine:'
"$READELF" -d "$OUTPUT" | grep -E 'NEEDED|RPATH|RUNPATH' || true
"$NM" -D "$OUTPUT" | grep custom_init_jsapis
