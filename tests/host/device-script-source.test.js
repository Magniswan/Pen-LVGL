const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');

const root = path.resolve(__dirname, '..', '..');
const read = (relative) => fs.readFileSync(path.join(root, relative), 'utf8');
const common = read('scripts/device_app_common.ps1');
const entrypoints = [
  'scripts/install_device_app.ps1',
  'scripts/uninstall_device_app.ps1',
  'scripts/status_device_app.ps1',
  'scripts/install_manager.ps1',
  'scripts/uninstall_manager.ps1',
];

test('every device script requires an explicit serial and identity digest', () => {
  for (const name of entrypoints) {
    const source = read(name);
    assert.match(source,
      /\[Parameter\(Mandatory\)\]\[ValidatePattern\('\^\[A-Za-z0-9\._:-\]\{1,128\}\$'\)\]\[string\]\$Serial/,
      name);
    assert.match(source,
      /\[Parameter\(Mandatory\)\]\[ValidatePattern\('\^\[A-Fa-f0-9\]\{64\}\$'\)\]\[string\]\$ExpectedIdentitySha256/,
      name);
    assert.match(source, /Set-DeviceTarget[\s\S]*Assert-DeviceTarget/, name);
    assert.doesNotMatch(source, /Assert-OneDevice|Set-DeviceAdb/, name);
  }
});

test('all operational adb calls are pinned to the selected serial', () => {
  assert.match(common, /\$targetedArguments = @\("-s", \$script:DeviceSerial\) \+ \$Arguments/);
  assert.match(common, /& \$script:DeviceAdb @targetedArguments/);
  const directInvocations = common.match(/& \$script:DeviceAdb[^\r\n]*/g) || [];
  assert.deepEqual(directInvocations, [
    '& $script:DeviceAdb @targetedArguments',
    '& $script:DeviceAdb devices',
  ]);
  assert.match(common, /\^\(\[\^\\s\]\+\)\\s\+\(\[\^\\s\]\+\)\$/);
  assert.match(common, /\$Matches\[1\] -eq \$script:DeviceSerial/);
  assert.match(common, /\$matches\.Count -ne 1/);
});

test('host identity digest mirrors the native manager evidence framing', () => {
  const manager = read('manager/native/src/Manager/Manager.cpp');
  for (const evidence of [
    '/etc/miniapp/resources/local_packages.json',
    '/etc/miniapp/resources/cfg.json',
  ]) {
    assert.match(common, new RegExp(evidence.replaceAll('/', '\\/')));
    assert.match(manager, new RegExp(evidence.replaceAll('/', '\\/')));
  }
  assert.match(common, /Get-DeviceText -Command "uname -m"/);
  assert.match(common, /\$stream\.WriteByte\(0\)/);
  assert.match(common, /SHA256\]::Create\(\)/);
  assert.match(manager, /append\(identity\.machine/);
  assert.match(manager, /evidence\.push_back\(0\)/);
  assert.match(manager, /crypto\.sha256\(evidence\.data\(\)/);
});

test('identity validation is fail closed', () => {
  assert.match(common, /\$Serial -notmatch '\^\[A-Za-z0-9\._:-\]\{1,128\}\$'/);
  assert.match(common, /\$ExpectedIdentitySha256 -match '\^0\{64\}\$'/);
  assert.match(common, /\$actualIdentity -cne \$script:ExpectedDeviceIdentitySha256/);
  assert.match(common, /absent, unauthorized, offline, or ambiguous/);
});
