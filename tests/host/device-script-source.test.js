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

test('every device script requires an explicit serial, without a device identity binding', () => {
  for (const name of entrypoints) {
    const source = read(name);
    assert.match(source,
      /\[Parameter\(Mandatory\)\]\[ValidatePattern\('\^\[A-Za-z0-9\._:-\]\{1,128\}\$'\)\]\[string\]\$Serial/,
      name);
    assert.match(source, /Set-DeviceTarget[\s\S]*Assert-DeviceTarget/, name);
    assert.doesNotMatch(source, /ExpectedIdentitySha256/, name);
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

test('selected serial validation is fail closed', () => {
  assert.match(common, /\$Serial -notmatch '\^\[A-Za-z0-9\._:-\]\{1,128\}\$'/);
  assert.match(common, /absent, unauthorized, offline, or ambiguous/);
  assert.doesNotMatch(common, /local_packages\.json|cfg\.json|SHA256\]::Create\(\)|ExpectedDeviceIdentity/);
});
