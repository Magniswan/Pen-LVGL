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
  assert.match(ui, /installer_\.scan_installed\(\)/);
  assert.match(ui, /installer_\.install\(scan_\.candidates\[selected_\]\.token\)/);
  assert.match(ui, /installer_\.rollback\(installed_scan_\.applications\[selected_\]\.token\)/);
  assert.match(ui, /installer_\.remove\(installed_scan_\.applications\[selected_\]\.token\)/);
  assert.match(ui, /show_confirmation\(operation\)/);
  assert.match(ui, /高水位不会下降/);
  assert.match(ui, /反回滚策略与应用私有数据会保留/);
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
  assert.match(daemon, /installed_application_snapshots\(\s*profile, \*crypto, installed_reliable\)/);
  assert.match(daemon, /rollback_installed_application\(/);
  assert.match(daemon, /remove_installed_application\(/);
  assert.match(daemon, /prepare_application_storage\(\)/);
  assert.match(daemon, /installer_policy\(profile\)/);
  assert.doesNotMatch(daemon, /LVGL_INSTALLER_(?:PATH|KEY|COMMAND)/);
});

test('installer protocol has a closed command and fixed packet surface', () => {
  const header = read('platform/include/lvgl_platform/installer_protocol.h');
  const protocol = read('platform/src/ipc/installer_protocol.cpp');
  assert.match(header, /installed_scan = 4/);
  assert.match(header, /installed_candidate = 5/);
  assert.match(header, /rollback = 6/);
  assert.match(header, /remove = 7/);
  assert.match(header, /kInstallerRequestSize = 160/);
  assert.match(header, /kInstallerResponseSize = 768/);
  assert.match(protocol, /valid_token\(/);
  assert.match(protocol, /token\.size\(\) == kTokenLimit/);
  assert.match(protocol, /all_zero\(/);
  assert.match(protocol, /'L', 'V', 'I', 'N', 'S', 'T', '2'/);
  assert.doesNotMatch(header, /path|public.?key|shell|command_text/i);
});
