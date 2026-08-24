const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');

const root = path.resolve(__dirname, '..', '..');
const source = fs.readFileSync(
  path.join(root, 'platform', 'src', 'services', 'inbox_service.cpp'), 'utf8');

test('inbox service has fixed roots and accepts only digest tokens', () => {
  assert.match(source, /kApplicationInboxRoot|lvgl-inbox/);
  assert.match(source, /valid_token\(/);
  assert.match(source, /token\.size\(\) == 128/);
  assert.doesNotMatch(source, /publicKeyPath|key_path|caller_path|package_path/);
});

test('inbox service opens regular files without following names as paths', () => {
  assert.match(source, /::openat\(inbox, name\.c_str\(\), O_RDONLY \| O_NOFOLLOW/);
  assert.match(source, /S_ISREG/);
  assert.match(source, /st_nlink != 1/);
  assert.match(source, /before\.st_ino == after\.st_ino/);
});

test('installation repeats official verification, policy, and split-root transaction', () => {
  assert.match(source, /OfficialTrustStore::compiled\(\)/);
  assert.match(source, /evaluate_install_policy\(/);
  assert.match(source, /load_release_state\(/);
  assert.match(source, /reserved_application_id\(/);
  assert.match(source, /install_official_package_with_state_root\(/);
  assert.doesNotMatch(source, /allow_development\s*=\s*true/);
});
