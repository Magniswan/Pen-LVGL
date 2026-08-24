const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const root = path.resolve(__dirname, '../..');
const read = (relative) => fs.readFileSync(path.join(root, relative), 'utf8');

test('sessiond grants only a quota-enforcing broker socket for private storage capability', () => {
  const source = read('platform/src/session/session_daemon.cpp');
  assert.match(source, /kApplicationDataDirectory = "lvgl-data"/);
  assert.match(source, /manifest\.capabilities[\s\S]*"storage\.private"/);
  assert.match(source, /ensure_private_storage\(foreground\)/);
  assert.match(source, /LVGL_APP_STORAGE_FD=/);
  assert.match(source, /O_DIRECTORY \| O_NOFOLLOW/);
  assert.match(source, /SOCK_SEQPACKET/);
  assert.match(source, /quota_exceeded/);
  assert.match(source, /policy\.limits\.data_mib/);
  assert.match(source, /policy\.limits\.maximum_files/);
  assert.match(source, /O_WRONLY \| O_CREAT \| O_EXCL \| O_NOFOLLOW/);
  assert.match(source, /::renameat\(directory/);
  assert.match(source, /::fsync\(directory\)/);
  assert.doesNotMatch(source, /LVGL_APP_STORAGE_(?:PATH|ROOT)/);
});

test('runtime storage accepts only canonical records over the broker protocol', () => {
  const source = read('src/runtime/app_storage.cpp');
  assert.match(source, /valid_storage_record_name/);
  assert.match(source, /SOCK_SEQPACKET/);
  assert.match(source, /SO_PEERCRED/);
  assert.match(source, /peer\.uid != 0/);
  assert.match(source, /encode_storage_request/);
  assert.match(source, /decode_storage_response/);
  assert.doesNotMatch(source, /openat\s*\(|renameat\s*\(|directory_fd_/);
  assert.doesNotMatch(source, /\/userdisk\/|system\s*\(|popen\s*\(/);
});

test('2048 persistence is fixed-size, checksummed, and never uses a path', () => {
  const source = read('apps/game-2048/game_2048_persistence.cpp');
  assert.match(source, /G2048V01/);
  assert.match(source, /crc32/);
  assert.match(source, /storage\.write_atomic\(kRecord/);
  assert.doesNotMatch(source, /\/userdisk\/|fstream|open\s*\(/);
});
