const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const launcherRoot = path.resolve(__dirname, '..');
const repositoryRoot = path.resolve(launcherRoot, '..');

function read(relativePath) {
  return fs.readFileSync(path.join(repositoryRoot, relativePath), 'utf8');
}

test('uses a persistent Falcon hole with an explicit touch overlay', () => {
  const page = read('launcher/src/pages/index/index.vue');
  assert.match(page, /<hole v-if="holeVisible" class="session-hole" :style="pageStyle"\s*\/>/);
  assert.match(page, /class="touch-layer"[\s\S]*?@touchstart="onTouchStart"[\s\S]*?@touchcancel="onTouchCancel"/);
  assert.match(page, /compactLayout\(\)[\s\S]*?logicalHeight < 180 \|\| this\.logicalWidth < 720/);
  assert.match(page, /onHide\(\) \{[\s\S]*?this\.cancelTouches\(\)[\s\S]*?this\.stopPolling\(\)/);
  assert.doesNotMatch(page, /stopSession|Launcher\.stop|miniapp_cli|\$appRuntime\.navigation\.exit/);
});

test('launches only the fixed native hole-session executable', () => {
  const native = read('launcher/native/src/Launcher/Launcher.cpp');
  assert.match(native, /\/userdisk\/apps\/lvgl-platform\/current\/bin\/lvgl-sessiond/);
  assert.match(native, /fexecve\(executable\.get\(\), arguments, environment\)/);
  assert.match(native, /--falcon-hole/);
  assert.match(native, /O_NOFOLLOW/);
  assert.match(native, /HOLE_SESSION_CERTIFIED/);
  assert.doesNotMatch(native, /\bexecve\(|\/bin\/(?:ba)?sh|system\s*\(|popen\s*\(/);
});

test('contains no process termination path for Falcon or LVGL', () => {
  const sources = [
    'launcher/native/src/Launcher/Launcher.cpp',
    'launcher/native/src/Launcher/JSLauncher.cpp',
    'launcher/src/pages/index/index.vue',
    'launcher/src/services/launcher.js',
    'scripts/device_app_common.ps1',
    'scripts/install_device_app.ps1',
    'scripts/uninstall_device_app.ps1',
    'scripts/status_device_app.ps1',
  ].map(read).join('\n');
  assert.doesNotMatch(sources, /killall|pkill|SIGTERM|SIGKILL|runDictPen|lvgl-supervisor/i);
  assert.deepEqual(sources.match(/\bkill\s*\([^)]*\)/g), ['kill(processId, 0)']);
  assert.doesNotMatch(sources, /miniapp_cli\s+(?:stop|kill)/i);
});

test('keeps platform payload ownership outside Falcon package scripts', () => {
  const install = read('scripts/install_device_app.ps1');
  const uninstall = read('scripts/uninstall_device_app.ps1');
  assert.match(install, /miniapp_cli install \/tmp\/lvgl-launcher\.amr/);
  assert.doesNotMatch(install, /\/userdisk\/apps\/lvgl-platform|tar\s|unzip\s|lvapp/i);
  assert.match(uninstall, /miniapp_cli uninstall \$DeviceAppId/);
  assert.doesNotMatch(uninstall, /Remove-Item|rm -|\/userdisk\/apps\/lvgl-platform|\/run\/lvgl-platform/i);
});

test('keeps the native touch frame aligned with the platform protocol', () => {
  const native = read('launcher/native/src/Launcher/Launcher.cpp');
  assert.match(native, /kTouchMagic = 0x4C565450U/);
  assert.match(native, /kTouchVersion = 1/);
  assert.match(native, /kTouchFrameSize = 56/);
  assert.match(native, /CLOCK_MONOTONIC/);
  assert.match(native, /SESSION_NONCE/);
});

test('uses the reserved launcher identity and production-facing version', () => {
  const packageJson = JSON.parse(read('launcher/package.json'));
  assert.equal(packageJson.appid, '8080992608050001');
  assert.equal(packageJson.version, '0.2.0');
  assert.equal(packageJson.singleJsBundle, undefined);
  assert.equal(packageJson['single-js-bundle'], false);
});

test('legacy supervisor and destructive device runners stay removed', () => {
  [
    'launcher/device/lvgl-supervisor.sh',
    'launcher/device/manifest.template.env',
    'scripts/run_m5.ps1',
    'scripts/device_run_poc.sh',
    'scripts/tests/test_supervisor.ps1',
  ].forEach((relativePath) => {
    assert.equal(fs.existsSync(path.join(repositoryRoot, relativePath)), false, relativePath);
  });
});
