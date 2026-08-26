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
  assert.match(source, /independently approved SHA-512 digest/);
  assert.match(source, /\bsign[\s\S]*--key \$SignerPrivate/);
  assert.match(source, /\bverify[\s\S]*--public-key \$SignerPublic/);
  assert.match(source, /signatureValid/);
  assert.match(source, /Refusing to overwrite signer output/);
  assert.doesNotMatch(source, /Get-Content.*SignerPrivate|WriteAll.*SignerPrivate|adb(?:\.exe)?\b/i);
});
