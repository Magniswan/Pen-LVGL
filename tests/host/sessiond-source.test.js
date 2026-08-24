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
    /open_verified_program\([\s\S]*release, release\.profile, foreground, &policy\)/,
  );
  assert.match(source, /foreground = "top\.lvgl\.desktop"/);
  assert.match(source, /应用异常退出/);
});

test('session daemon applies independent identity, resource limits, and inherited devices', () => {
  assert.match(source, /RLIMIT_AS/);
  assert.match(source, /RLIMIT_CPU/);
  assert.match(source, /RLIMIT_NPROC/);
  assert.match(source, /::setgroups\(0, nullptr\)/);
  assert.match(source, /::setresgid\(policy\.gid/);
  assert.match(source, /::setresuid\(policy\.uid/);
  assert.match(source, /application_uid_is_unique\(app_id\)/);
  assert.match(source, /PR_SET_DUMPABLE/);
  assert.match(source, /LVGL_DRM_FD=/);
  assert.match(source, /LVGL_SANDBOX_REQUIRED=1/);
  assert.match(source, /LVGL_APP_UID=/);
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

test('failed dynamic releases roll back only after previous bytes are reverified', () => {
  assert.match(source, /rollback_application_after_failure\(/);
  assert.match(source, /active\.previous_release/);
  assert.match(source, /active\.previous_digest/);
  assert.match(source, /verify_application_release\([\s\S]*active\.previous_release/);
  assert.match(source, /rollback_failed_release\(active\)/);
  assert.match(source, /persist_release_state\(/);
  assert.match(source, /应用失败版本已隔离并安全回滚到上一版本/);
});

test('only a signed platform built-in may be used after a dynamic override is absent', () => {
  assert.match(source, /if\(built_in_program_path\(app_id\) == nullptr\) return \{\};/);
  assert.match(source, /const char\* path = built_in_program_path\(app_id\)/);
  assert.match(source, /open_release_file\(release\.directory\.get\(\), path\)/);
  assert.match(source, /trusted_regular\(executable\.get\(\), 0755/);
});
