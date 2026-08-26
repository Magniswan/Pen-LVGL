const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const root = path.resolve(__dirname, '../..');
const read = (relative) => fs.readFileSync(path.join(root, relative), 'utf8');

test('platform staging accepts only clean certified production inputs', () => {
  const source = read('scripts/stage_platform_release.ps1');
  assert.match(source, /status --porcelain=v1 --untracked-files=all/);
  assert.match(source, /LVGL_PLATFORM_PRODUCTION_BUILD:BOOL=ON/);
  assert.match(source, /LVGL_PLATFORM_OFFICIAL_PUBLIC_KEY_HEX:STRING=/);
  assert.match(source, /CMAKE_HOME_DIRECTORY:INTERNAL/);
  assert.doesNotMatch(source, /\$home\b/i);
  assert.match(source, /CMAKE_COMMAND:INTERNAL/);
  assert.match(source, /CMAKE_MAKE_PROGRAM/);
  assert.match(source, /exactly CMake 3\.31\.6 and Ninja 1\.12\.1/);
  assert.match(source, /wsl\.exe -d \$WslDistribution -- \$buildTooling\.CMake/);
  assert.match(source, /wslpath -a/);
  assert.doesNotMatch(source, /bash -lc.*wslpath/);
  assert.match(source, /--clean-first --target[\s\S]*lvgl_sessiond[\s\S]*game_2048/);
  assert.match(source, /Source changed during the clean production rebuild/);
  assert.match(source, /exactly Node v18\.20\.8/);
  assert.match(source, /HOLE_SESSION_CERTIFIED.*-ne '1'/s);
  assert.match(source, /ELF64 little-endian AArch64/);
  assert.match(source, /ReparsePoint/);
  assert.match(source, /Refusing to overwrite release output/);
  assert.doesNotMatch(source, /PrivateKey|\bsign\b|adb(?:\.exe)?\b/i);
});

test('platform staging emits deterministic handoff evidence', () => {
  const source = read('scripts/stage_platform_release.ps1');
  for (const artifact of [
    'platform.lvapp.dev', 'inspection.json', 'manifest.source.json',
    'build-evidence.json', 'sbom.spdx.json', 'THIRD_PARTY-NOTICES.txt',
    'checksums.txt',
  ]) {
    assert.match(source, new RegExp(artifact.replaceAll('.', '\\.')));
  }
  assert.match(source, /git.*show -s --format=%ct HEAD/);
  assert.match(source, /appId = 'top\.lvgl\.platform'/);
  assert.match(source, /entry = 'bin\/lvgl-sessiond'/);
  assert.match(source, /Move-Item -LiteralPath \$artifact -Destination \$ReleaseOutput/);
});

test('offline signer binds private-key output to the pinned official public key', () => {
  const source = read('scripts/sign_release.ps1');
  assert.match(source, /\.lvapp\.dev/);
  assert.match(source, /Private signing keys must never be stored inside the repository/);
  assert.match(source, /key-info[\s\S]*--public-key/);
  assert.match(source, /rawPublicKeyHex -cne \$ExpectedPublicKeyHex/);
  assert.match(source, /ExpectedDevelopmentSha512/);
  assert.match(source, /ExpectedSourceCommit/);
  assert.match(source, /exact reviewed clean source commit/);
  assert.match(source, /exactly Node v18\.20\.8/);
  assert.match(source, /source-evidence\.json/);
  assert.match(source, /independently approved SHA-512 digest/);
  assert.match(source, /\bsign[\s\S]*--key \$SignerPrivate/);
  assert.match(source, /\bverify[\s\S]*--public-key \$SignerPublic/);
  assert.match(source, /signatureValid/);
  assert.match(source, /Refusing to overwrite signer output/);
  assert.doesNotMatch(source, /Get-Content.*SignerPrivate|WriteAll.*SignerPrivate|adb(?:\.exe)?\b/i);
});

test('Falcon release builders pin toolchains and inspect exact AMR contents', () => {
  const common = read('scripts/falcon_build_common.ps1');
  const launcher = read('scripts/build_launcher.ps1');
  const manager = read('scripts/build_manager.ps1');
  assert.match(common, /v18\.20\.8/);
  assert.match(common, /aiot-vue-cli.*1\.0\.32/s);
  assert.match(common, /aiot-vue-cli\/src\/cli\.js/);
  assert.match(common, /\$NodeExecutable \$builder -c -q -p/);
  assert.match(common, /\$NodeExecutable \$builder -p/);
  assert.match(common, /ValidateSet\('app\.js', 'app\.js\.bin'\)/);
  assert.match(common, /ScriptEntry.*app_icon\.png.*NativeLibrary.*manifest\.json/s);
  assert.match(common, /ComputeHash\(\$stream\)/);
  assert.match(common, /Get-FileHash.*SHA256/);
  assert.match(common, /ConvertTo-DeterministicFalconArchive/);
  assert.match(common, /1980, 1, 1, 0, 0, 0/);
  assert.match(common, /Get-FalconArchiveReport[\s\S]*\[IO\.File\]::Move[\s\S]*Get-FalconArchiveReport/);
  assert.match(launcher, /launcher\/tools\/build-native\.sh/);
  assert.match(launcher, /libjsapi_lvgl_launcher\.so/);
  assert.match(manager, /manager\/tools\/build-native\.sh/);
  assert.match(manager, /libjsapi_lvgl_manager\.so/);
  assert.doesNotMatch(`${launcher}\n${manager}`, /bash -lc.*wslpath/);
  assert.doesNotMatch(`${common}\n${launcher}\n${manager}`, /adb(?:\.exe)?\b/i);
});

test('both Falcon packages declare the exact release Node version', () => {
  for (const relative of ['launcher/package.json', 'manager/package.json']) {
    const manifest = JSON.parse(read(relative));
    assert.equal(manifest.engines.node, '18.20.8');
  }
});
