# Changelog - Release Tooling

> 最新变更在最上方。

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
