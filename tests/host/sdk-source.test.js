const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const root = path.resolve(__dirname, '../..');
const read = (relative) => fs.readFileSync(path.join(root, relative), 'utf8');

test('SDK helper enforces canonical app IDs and shared runtime linkage', () => {
  const helper = read('cmake/LvglApplication.cmake');
  assert.match(helper, /APP_ID must be a lowercase reverse-domain identifier/);
  assert.match(helper, /platform_runtime[\s\S]*app_shell/);
  assert.match(helper, /lvgl_platform::sdk_headers/);
  assert.match(helper, /LVGL_APPLICATION_ID/);
  assert.match(helper, /LVGL_PLATFORM_SDK_ABI="1\.0"/);
  assert.match(helper, /-Wall -Wextra -Wpedantic -Werror/);
  assert.doesNotMatch(helper, /PROJECT_SOURCE_DIR.*src/);
});

test('SDK publishes a versioned public header surface', () => {
  const runtime = read('sdk/include/lvgl_platform/runtime.hpp');
  const umbrella = read('sdk/include/lvgl_platform/sdk.hpp');
  assert.match(runtime, /sdk_abi_major\s*=\s*1/);
  assert.match(runtime, /sdk_abi_minor\s*=\s*0/);
  assert.match(runtime, /struct AppContext/);
  assert.match(runtime, /using RuntimeContext = AppContext/);
  assert.match(runtime, /class RuntimeApplication/);
  assert.match(umbrella, /lvgl_platform\/runtime\.hpp/);
  assert.match(umbrella, /lvgl_platform\/app_storage\.hpp/);
  assert.match(umbrella, /lvgl_platform\/shell\.hpp/);
});

test('template has a production-shaped manifest and no direct device access', () => {
  const manifest = JSON.parse(read('sdk/template-app/app.json'));
  assert.equal(manifest.appId, 'top.example.hello');
  assert.equal(manifest.sdkAbi, '1.0');
  assert.equal(manifest.entry, 'bin/lvgl-example');
  assert.equal(manifest.onlinePolicy.mode, 'offline-v1');
  const source = read('sdk/template-app/main.cpp');
  assert.match(source, /<lvgl_platform\/sdk\.hpp>/);
  assert.doesNotMatch(source, /"(?:runtime|shell)\//);
  assert.match(source, /RuntimeApplication/);
  assert.match(source, /AppShell/);
  assert.doesNotMatch(source, /\/dev\/|\/userdisk\/|system\s*\(|popen\s*\(|execv/);
});

test('2048 reference application consumes only the public SDK surface', () => {
  const main = read('apps/game-2048/main.cpp');
  const ui = read('apps/game-2048/game_2048_ui.cpp');
  const persistence = read('apps/game-2048/game_2048_persistence.cpp');
  assert.match(main, /<lvgl_platform\/sdk\.hpp>/);
  for (const source of [main, ui, persistence]) {
    assert.doesNotMatch(source, /"(?:runtime|shell)\//);
  }
});

test('reference 2048 manifest matches the registered stable identity', () => {
  const manifest = JSON.parse(read('apps/game-2048/app.json'));
  assert.equal(manifest.appId, 'top.lvgl.game2048');
  assert.equal(manifest.entry, 'bin/lvgl-2048');
  assert.deepEqual(manifest.capabilities, ['storage.private']);
});

test('developer packaging script emits dev packages and never accepts signing material', () => {
  const source = read('sdk/package-app.ps1');
  assert.match(source, /\.lvapp\.dev/);
  assert.match(source, /tools\/lvapp\/cli\.mjs/);
  assert.match(source, /\bbuild\b/);
  assert.doesNotMatch(source, /private|signingKey|publicKey|\bsign\b/i);
});
