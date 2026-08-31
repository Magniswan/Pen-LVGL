const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');

const root = path.resolve(__dirname, '..', '..');
const sandbox = fs.readFileSync(
  path.join(root, 'src', 'runtime', 'runtime_sandbox.cpp'), 'utf8');
const runtime = fs.readFileSync(
  path.join(root, 'src', 'runtime', 'platform_runtime.cpp'), 'utf8');

test('sandbox installs a syscall filter when enabled and permits personal root mode', () => {
  assert.match(sandbox, /LVGL_SANDBOX_REQUIRED/);
  assert.match(sandbox, /mandatory runtime sandbox is unavailable/);
  assert.match(sandbox, /AUDIT_ARCH_AARCH64/);
  assert.match(sandbox, /PR_SET_NO_NEW_PRIVS/);
  assert.match(sandbox, /PR_SET_SECCOMP/);
  assert.match(sandbox, /SECCOMP_MODE_FILTER/);
  assert.match(sandbox, /SECCOMP_RET_KILL_PROCESS/);
  assert.match(sandbox, /strcmp\(marker, "0"\) == 0/);
  assert.match(runtime, /install_runtime_sandbox\(sandbox_error\)/);
});

test('sandbox exposes no process creation, network creation, or write-path syscall', () => {
  assert.doesNotMatch(sandbox, /__NR_(?:execve|clone|fork|vfork|socket|connect|bind|listen|accept|mount|ptrace|kill)\b/);
  assert.doesNotMatch(sandbox, /__NR_(?:unlinkat|renameat|mkdirat|mknodat|chmod|chown|truncate|ftruncate)\b/);
  assert.match(sandbox, /O_WRONLY \| O_RDWR \| O_CREAT \| O_TRUNC \| O_APPEND/);
  assert.match(sandbox, /PROT_EXEC/);
});

test('DRM ioctl access is bound to one descriptor and an explicit request set', () => {
  assert.match(sandbox, /offsetof\(seccomp_data, args\[0\]\)/);
  assert.match(sandbox, /static_cast<std::uint32_t>\(drm_fd\)/);
  assert.match(sandbox, /DRM_IOCTL_MODE_SETPLANE/);
  assert.match(sandbox, /DRM_IOCTL_MODE_RMFB/);
  assert.match(sandbox, /DRM_IOCTL_MODE_DESTROY_DUMB/);
  assert.doesNotMatch(sandbox, /DRM_IOCTL_MODE_CREATE_DUMB/);
  assert.doesNotMatch(sandbox, /DRM_IOCTL_MODE_ADDFB2/);
  assert.doesNotMatch(sandbox, /append_allowed_syscall\(filter, __NR_ioctl\)/);
});
