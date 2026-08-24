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

test('session daemon supervises one independent foreground application', () => {
  assert.match(source, /ForegroundAction::launch/);
  assert.match(source, /SessionControlCommand::launch_application/);
  assert.match(source, /SessionControlCommand::home/);
  assert.match(source, /run_foreground\(/);
  assert.match(
    source,
    /open_verified_program\([\s\S]*release, release\.profile, foreground, &private_storage\)/,
  );
  assert.match(source, /foreground = "top\.lvgl\.desktop"/);
  assert.match(source, /应用异常退出/);
});

test('dynamic applications are remeasured from official packages and anti-rollback state', () => {
  assert.match(source, /OfficialTrustStore::compiled\(\)/);
  assert.match(source, /load_release_state\(/);
  assert.match(source, /active\.current_digest/);
  assert.match(source, /installed_files_match\(/);
  assert.match(source, /verify_installed_application\(/);
  assert.match(source, /manifest\.entry/);
  assert.match(source, /atomic_registry\(/);
  assert.match(source, /LVGL_APP_REGISTRY_FD=/);
});
