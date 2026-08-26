# Changelog - Release Tooling

> 最新变更在最上方。

## [2026-08-26] 使 Falcon AMR 外层可复现

**类型**: reproducibility-fix
**提交**: b94b196
**风险**: MEDIUM

- 原 Falcon CLI ZIP timestamps 被规范化为固定时间/顺序，规范化前后均执行精确 AMR 校验。
- Launcher production 两次从头构建外层 SHA-256 一致；Manager development 模式通过同一路径。
- 回滚：`git revert b94b196` 会恢复外层 hash 漂移。

## [2026-08-26] 固定 WSL release 工具与路径

**类型**: build-security-fix
**提交**: 76bb7f2
**风险**: HIGH

- 相关提交：`ae913ae`、`d56fb85`、`15af7ca`、`76bb7f2`。
- 修复 reserved `$HOME` 冲突、WSL cache 路径比较、非登录 PATH 与丢失 `$1` 的 shell 参数传递。
- stager 从 cache 取得并验证普通非链接的 CMake 3.31.6/Ninja 1.12.1，直接在指定 WSL 中 clean rebuild。
- 三次完整 staging 产物一致；最终 evidence 绑定代码提交 `b94b196`。
- 回滚这些提交会破坏 source-bound staging，禁止用于发布。

## [2026-08-26] 将发布产物绑定到 reviewer-approved source

**类型**: security-fix
**提交**: 16765df
**风险**: HIGH

- stager 验证 CMake home 指向当前 clean repository，重新 configure 并 `--clean-first` 构建四个 production targets，构建后再次检查 source/HEAD 未变化。
- stager/signer 均固定 Node 18.20.8；signer 新增 40-hex `ExpectedSourceCommit`，要求精确 clean commit 并输出 `source-evidence.json`。
- 修复“build evidence 记录当前 commit，但 payload 实际来自旧 build directory”的证明缺口。
- 回滚：`git revert 16765df` 会恢复 stale-build/未绑定 signer source 风险，禁止用于正式发布。

## [2026-08-26] 固定 Falcon AMR 构建与归档检查

**类型**: security
**提交**: 16d557f
**风险**: HIGH

- 精确固定 Node 18.20.8 与 `aiot-vue-cli 1.0.32`。
- launcher/manager 统一验证 native bridge、AMR 四条目、manifest cert MD5 与外层 SHA-256。
- production `app.js.bin` 和 development `app.js` 分模式验证，脚本无 ADB 路径。
- 回滚：`git revert 16d557f`；会恢复未检查的工具链/归档输出。

## [2026-08-26] 新增失败关闭的离线发布交接

**类型**: feat
**提交**: 381027e
**风险**: HIGH

### 变更文件

| 文件 | 说明 |
|---|---|
| `scripts/stage_platform_release.ps1` | 从 clean production build 生成确定性 dev 包、evidence、SPDX SBOM/notices 与双摘要 |
| `scripts/sign_release.ps1` | 把仓库外私钥签名绑定到批准 SHA-512 和精确官方 raw public key |
| `tools/lvapp/cli.mjs` / `lib.mjs` | 新增 Ed25519 `key-info` |
| `tests/host/release-source.test.js` | 固化 clean build、no-private-key、no-ADB、key/digest binding |

### 影响范围

- **职责分离**: builder/reviewer/offline signer/publisher 具备可执行交接物。
- **产物**: 新增 build evidence、manifest source、inspection、SPDX 2.3、notices、SHA-256/SHA-512。
- **验证**: production-mode AArch64 rehearsal 两次 staging 字节一致；使用审计 key/profile，仅为工具链证据，不是正式发布。
- **回滚**: `git revert 381027e` 会移除标准离线交接入口，不影响既有包格式。

## [2026-08-24] 固定 ADB 目标并复验设备 identity

**类型**: security
**提交**: ba391e3
**风险**: HIGH

### 变更文件

| 文件 | 说明 |
|---|---|
| `scripts/device_app_common.ps1` | 所有业务命令强制 `-s`，按 manager framing 复算 identity |
| `scripts/install_device_app.ps1` / `uninstall_device_app.ps1` / `status_device_app.ps1` | serial/digest 改为必填 |
| `scripts/install_manager.ps1` / `uninstall_manager.ps1` | serial/digest 改为必填 |
| `tests/host/device-script-source.test.js` | 覆盖 target pinning、framing 与失败关闭契约 |

### 影响范围

- **兼容性**: 旧的无 serial/digest 调用现在失败；自动化必须从认证记录显式传入。
- **安全边界**: 防止多设备环境误操作，但不把 operator-provided digest 声明为 root-resistant attestation。
- **回滚**: `git revert ba391e3` 会恢复隐式设备选择，禁止在生产操作链使用。

## [2026-08-24] 新增确定性签名应用包工具

**类型**: feat  
**提交**: 0b80d22  
**风险**: HIGH

### 变更文件

| 文件 | 变更 | 说明 |
|---|---:|---|
| `tools/lvapp/.nvmrc` | +1/-0 | 固定 Node 18.20.8 |
| `tools/lvapp/lib.mjs` | +447/-0 | canonical 包、Ed25519 签名和严格解析验证 |
| `tools/lvapp/cli.mjs` | +65/-0 | build/sign/inspect/verify CLI |
| `tools/lvapp/test/lvapp.test.mjs` | +151/-0 | 可复现、篡改、路径与 schema 测试 |
| `tools/lvapp/package.json` | +11/-0 | 固定 Node 18.20.8 工具入口 |
| `tools/lvapp/package-lock.json` | +15/-0 | 零第三方依赖 lockfile |

### 影响范围

- **API**: 新增 LVAPP library 与 CLI。
- **跨模块**: native verifier、设备 installer 和所有应用发布。
- **数据模型**: LVAPP001/LVSIG001、canonical manifest、file table。
- **配置**: 离线 Ed25519 key 与固定 Node 版本。

### 回滚指南

- 回滚：`git revert 0b80d22`
- 检查：整个 `tools/lvapp` 与 native format compatibility。
- 副作用：无法生成与验证可安装的正式应用包。

## [2026-08-24] 收紧 Falcon 设备脚本边界

**类型**: refactor  
**提交**: 81116fe  
**风险**: HIGH

### 变更文件

| 文件 | 变更 | 说明 |
|---|---:|---|
| `launcher/tools/build-native.sh` | +26/-4 | 固定 AArch64 plugin 构建和 ELF 检查 |
| `scripts/device_app_common.ps1` | +0/-58 | 删除旧 shared helper |
| `scripts/device_run_poc.sh` | +0/-150 | 删除旧破坏性设备 runner |
| `scripts/install_device_app.ps1` | +17/-101 | 只安装 launcher AMR，不写平台 payload |
| `scripts/run_m5.ps1` | +0/-115 | 删除旧设备入口 |
| `scripts/status_device_app.ps1` | +10/-20 | 只读状态收集 |
| `scripts/tests/test_supervisor.ps1` | +0/-119 | 删除旧 kill supervisor 测试 |
| `scripts/uninstall_device_app.ps1` | +7/-36 | 只卸载 launcher AMR |

### 影响范围

- **API**: host scripts 参数收紧。
- **跨模块**: Falcon launcher 发布与平台 payload ownership。
- **数据模型**: 无。
- **配置**: AArch64 toolchain path。

### 回滚指南

- 回滚：`git revert 81116fe`
- 检查：所有 launcher install/uninstall scripts。
- 副作用：会恢复能触碰 payload/进程的旧脚本，不得用于生产。
