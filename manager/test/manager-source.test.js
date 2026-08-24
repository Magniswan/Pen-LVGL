const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const root = path.resolve(__dirname, '..');
const read = (relative) => fs.readFileSync(path.join(root, relative), 'utf8');

test('exposes exactly four fixed management operations', () => {
  const service = read('src/services/manager.js');
  assert.match(service, /\['install', 'repair', 'upgrade', 'remove'\]\.includes\(operation\)/);
  assert.doesNotMatch(service, /path|command|shell|publicKey|signingKey|packageBytes/i);
  const page = read('src/pages/index/index.vue');
  ['install', 'repair', 'upgrade', 'remove'].forEach((operation) => {
    assert.match(page, new RegExp(`id: '${operation}'`));
  });
});

test('requires an explicit second action before removal', () => {
  const page = read('src/pages/index/index.vue');
  assert.match(page, /operation === 'remove'[\s\S]*?confirmingRemove = true/);
  assert.match(page, /confirmRemove\(\)[\s\S]*?this\.execute\('remove'\)/);
  assert.match(page, /不会移除本管理器，也不会向 Falcon 或 miniapp 发送终止信号/);
});

test('has no override, key import, filesystem, or shell controls', () => {
  const source = [
    read('src/pages/index/index.vue'),
    read('src/services/manager.js'),
    read('src/services/manager-state.js'),
  ].join('\n');
  assert.doesNotMatch(source, /install anyway|仍然安装|忽略校验|导入密钥|选择公钥/i);
  assert.doesNotMatch(source, /\bexec\s*\(|\bspawn\s*\(|\bsystem\s*\(|\bpopen\s*\(|rm -|Remove-Item|killall|pkill/i);
  assert.doesNotMatch(source, /<input|<textarea|file-picker|chooseFile/i);
});

test('uses the reserved manager identity separate from the launcher', () => {
  const packageJson = JSON.parse(read('package.json'));
  assert.equal(packageJson.appid, '8080992608050002');
  assert.equal(packageJson.version, '0.1.0');
  assert.equal(packageJson['single-js-bundle'], false);
});

test('native JSAPI accepts no arguments or caller-selected paths', () => {
  const bridge = read('native/src/Manager/JSManager.cpp');
  const service = read('native/src/Manager/Manager.cpp');
  assert.match(bridge, /requireNoArguments\(info, name\)/);
  assert.match(bridge, /ManagerOperation::install/);
  assert.match(bridge, /ManagerOperation::repair/);
  assert.match(bridge, /ManagerOperation::upgrade/);
  assert.match(bridge, /ManagerOperation::remove/);
  assert.match(service, /constexpr const char\* kPlatformRoot = "\/userdisk\/apps\/lvgl-platform"/);
  assert.match(service, /constexpr const char\* kPolicyRoot = "\/userdisk\/apps\/lvgl-platform-policy"/);
  assert.match(service, /prepare_application_storage\(\)/);
  assert.doesNotMatch(service, /\bsystem\s*\(|\bpopen\s*\(|\bexecv|\/bin\/(?:ba)?sh|killall|pkill/);
});

test('removal is no-follow, bounded, isolated, and retains policy state', () => {
  const service = read('native/src/Manager/Manager.cpp');
  assert.match(service, /AT_SYMLINK_NOFOLLOW/);
  assert.match(service, /O_NOFOLLOW/);
  assert.match(service, /kRemovalNodeLimit = 100000/);
  assert.match(service, /kRemovalDepthLimit = 32/);
  assert.match(service, /\.lvgl-platform-removed-/);
  assert.match(service, /cleanupRemovalTombstones\(parent\.get\(\)\)/);
  assert.match(service, /anti-rollback policy and manager retained/);
  assert.doesNotMatch(service, /removeContents\([^)]*kPolicyRoot/);
});

test('production native build fails closed without every provisioned input', () => {
  const build = read('tools/build-native.sh');
  [
    'OFFICIAL_PUBLIC_KEY_HEX',
    'MANAGER_CERTIFIED_PROFILE_ID',
    'MANAGER_CERTIFIED_MACHINE',
    'MANAGER_DEVICE_IDENTITY_SHA256_HEX',
    'MANAGER_PLATFORM_PACKAGE',
  ].forEach((name) => assert.match(build, new RegExp(`requires ${name}`)));
  assert.match(build, /key == '0' \* 64 or key == rfc_key/);
  assert.match(build, /stat\.S_ISREG/);
});

test('host install scripts manage only the manager AMR', () => {
  const install = fs.readFileSync(path.resolve(root, '../scripts/install_manager.ps1'), 'utf8');
  const uninstall = fs.readFileSync(path.resolve(root, '../scripts/uninstall_manager.ps1'), 'utf8');
  assert.match(install, /miniapp_cli install \/tmp\/lvgl-manager\.amr/);
  assert.match(uninstall, /miniapp_cli uninstall \$DeviceAppId/);
  assert.doesNotMatch(`${install}\n${uninstall}`, /\/userdisk\/apps\/lvgl-platform|Remove-Item|rm -|killall|pkill/i);
});
