const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');

const root = path.resolve(__dirname, '..', '..');
const read = (relative) => fs.readFileSync(path.join(root, relative), 'utf8');

test('non-root installer UI uses only the inherited typed broker', () => {
  const ui = read('apps/installer/installer_ui.cpp');
  const client = read('src/runtime/installer_client.cpp');
  assert.match(ui, /installer_\.scan\(\)/);
  assert.match(ui, /installer_\.install\(token\)/);
  assert.doesNotMatch(ui, /scan_official_inbox|install_official_inbox_candidate|prepare_application_storage|CryptoProvider/);
  assert.match(client, /LVGL_INSTALLER_FD/);
  assert.match(client, /SOCK_SEQPACKET/);
  assert.match(client, /SO_PEERCRED/);
  assert.match(client, /peer\.uid != 0/);
  assert.doesNotMatch(client, /publicKey|package_path|caller_path|system\s*\(|popen\s*\(/i);
});

test('sessiond grants the broker only to the fixed built-in installer', () => {
  const daemon = read('platform/src/session/session_daemon.cpp');
  assert.match(daemon, /installer_program = program_id == "top\.lvgl\.installer"/);
  assert.match(daemon, /LVGL_INSTALLER_FD=/);
  assert.match(daemon, /handle_installer_request\(parent_installer\.get\(\), profile\)/);
  assert.match(daemon, /install_official_inbox_candidate\([\s\S]*request\.token/);
  assert.match(daemon, /prepare_application_storage\(\)/);
  assert.match(daemon, /installer_policy\(profile\)/);
  assert.doesNotMatch(daemon, /LVGL_INSTALLER_(?:PATH|KEY|COMMAND)/);
});

test('installer protocol has a closed command and fixed packet surface', () => {
  const header = read('platform/include/lvgl_platform/installer_protocol.h');
  const protocol = read('platform/src/ipc/installer_protocol.cpp');
  assert.match(header, /InstallerCommand : std::uint16_t \{ scan = 1, candidate = 2, install = 3 \}/);
  assert.match(header, /kInstallerRequestSize = 160/);
  assert.match(header, /kInstallerResponseSize = 768/);
  assert.match(protocol, /valid_token\(/);
  assert.match(protocol, /token\.size\(\) == kTokenLimit/);
  assert.match(protocol, /all_zero\(/);
  assert.doesNotMatch(header, /path|public.?key|shell|command_text/i);
});
