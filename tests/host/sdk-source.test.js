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
  assert.match(helper, /LVGL_APPLICATION_ID/);
  assert.match(helper, /-Wall -Wextra -Wpedantic -Werror/);
});

test('template has a production-shaped manifest and no direct device access', () => {
  const manifest = JSON.parse(read('sdk/template-app/app.json'));
  assert.equal(manifest.appId, 'top.example.hello');
  assert.equal(manifest.sdkAbi, '1.0');
  assert.equal(manifest.entry, 'bin/lvgl-example');
  assert.equal(manifest.onlinePolicy.mode, 'offline-v1');
  const source = read('sdk/template-app/main.cpp');
  assert.match(source, /RuntimeApplication/);
  assert.match(source, /AppShell/);
  assert.doesNotMatch(source, /\/dev\/|\/userdisk\/|system\s*\(|popen\s*\(|execv/);
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
