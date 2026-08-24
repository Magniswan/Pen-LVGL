const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');

const root = path.resolve(__dirname, '..', '..');
const read = (relative) => fs.readFileSync(path.join(root, relative), 'utf8');

test('fuzz targets require Clang and instrument the parser library', () => {
  const cmake = read('CMakeLists.txt');
  assert.match(cmake, /LVGL_BUILD_FUZZERS requires Clang with libFuzzer/);
  assert.match(cmake, /set\(LVGL_ENABLE_SANITIZERS ON/);
  assert.match(cmake, /-fsanitize=address,undefined/);
  for (const target of [
    'fuzz_package_verifier',
    'fuzz_release_state',
    'fuzz_session_protocol',
  ]) {
    assert.match(cmake, new RegExp(`add_executable\\(${target}`));
  }
});

test('fuzz harnesses cover package, state, and external session bytes', () => {
  const packageHarness = read('tests/fuzz/fuzz_package_verifier.cpp');
  assert.match(packageHarness, /verify_package\(data, size/);
  assert.match(packageHarness, /CryptoProvider::load_default/);

  const stateHarness = read('tests/fuzz/fuzz_release_state.cpp');
  assert.match(stateHarness, /decode_release_state\(data, size/);
  assert.match(stateHarness, /select_release_state\(slot_a, slot_b/);

  const sessionHarness = read('tests/fuzz/fuzz_session_protocol.cpp');
  for (const parser of [
    'decode_session_control',
    'decode_application_registry',
    'decode_installer_request',
    'decode_installer_response',
    'decode_storage_request',
    'decode_storage_response',
    'decode_touch_frame',
    'parse_session_profile',
    'parse_device_profile',
  ]) {
    assert.match(sessionHarness, new RegExp(`${parser}\\(`));
  }
});

test('security workflow is bounded, preserves crashes, and pins actions', () => {
  const workflow = read('.github/workflows/security-fuzz.yml');
  assert.match(workflow, /-max_total_time=30/);
  assert.match(workflow, /-max_len=1048576/);
  assert.match(workflow, /ASAN_OPTIONS:/);
  assert.match(workflow, /UBSAN_OPTIONS:/);
  assert.match(workflow, /if: failure\(\)[\s\S]*upload-artifact/);
  assert.doesNotMatch(workflow, /uses: actions\/(?:checkout|upload-artifact)@v\d/);
});
