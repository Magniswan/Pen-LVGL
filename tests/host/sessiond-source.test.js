const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');

const root = path.resolve(__dirname, '..', '..');
const source = fs.readFileSync(
  path.join(root, 'platform', 'src', 'session', 'session_daemon.cpp'), 'utf8');

test('session daemon has one fixed signed-release execution path', () => {
  assert.match(source, /--falcon-hole/);
  assert.match(source, /OfficialTrustStore::compiled\(\)/);
  assert.match(source, /installed_files_match/);
  assert.match(source, /bin\/lvgl-desktop/);
  assert.match(source, /::fexecve\(executable/);
  assert.doesNotMatch(source, /execv\s*\(/);
  assert.doesNotMatch(source, /--bin-dir/);
});

test('session daemon never signals Falcon or miniapp processes', () => {
  assert.doesNotMatch(source, /miniapp/);
  assert.doesNotMatch(source, /Falcon/);
  assert.match(source, /::kill\(child, SIGTERM\)/);
  assert.match(source, /::kill\(child, SIGKILL\)/);
});

test('hole readiness waits for authenticated child control readiness', () => {
  const childReady = source.indexOf('SessionControlCommand::ready');
  const publishReady = source.indexOf('status.hole_ready = true');
  assert.ok(childReady >= 0 && publishReady > childReady);
  assert.match(source, /kReadyTimeoutMilliseconds/);
  assert.match(source, /TouchRouter router/);
});
