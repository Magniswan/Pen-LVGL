# Changelog - Falcon AMRs

> 最新变更在最上方。

## [2026-08-26] 固定并校验 Falcon AMR 构建链

**类型**: security
**提交**: 16d557f
**风险**: HIGH

### 变更文件

| 文件 | 说明 |
|---|---|
| `scripts/falcon_build_common.ps1` | 固定 Node/packager、精确检查 AMR 条目与证书摘要 |
| `scripts/build_launcher.ps1` | 新增无 ADB 的 launcher native + AMR 统一入口 |
| `scripts/build_manager.ps1` | manager 改用相同锁定入口并输出 AMR SHA-256 |
| `launcher/package.json`、`manager/package.json` | 声明精确 Node 18.20.8 |
| `tests/host/release-source.test.js` | 固化工具链、归档和 no-ADB 约束 |

### 影响范围

- **兼容性**: 其他 Node 版本和全局 CLI 现在失败关闭。
- **产物**: production `app.js.bin` 与 development `app.js` 分别精确验证。
- **验证**: launcher production、manager development AMR 已实跑通过；尚未宣称真机认证。
- **回滚**: `git revert 16d557f` 会恢复工具链漂移与未检查归档，禁止用于正式发布。

## [2026-08-24] 新增仅官方发布的 Falcon 平台管理器

**类型**: feat  
**提交**: 1710ecc  
**风险**: HIGH

### 变更文件

| 文件 | 变更 | 说明 |
|---|---:|---|
| `.gitignore` | +5/-0 | 忽略 manager 构建产物 |
| `manager/api-mock/lvgl_manager.js` | +42/-0 | 开发预览 mock |
| `manager/native/include/lvgl_platform/official_key_config.h` | +9/-0 | 未 provisioning key 占位 |
| `manager/native/include/manager_build_config.h` | +13/-0 | 未 provisioning build 占位 |
| `manager/native/include/manager_embedded_payload.h` | +11/-0 | 内嵌 payload 声明 |
| `manager/native/src/JSAPI.cpp` | +23/-0 | QuickJS module 注册 |
| `manager/native/src/Manager/Manager.cpp` | +494/-0 | identity、验签、安装和有界移除事务 |
| `manager/native/src/Manager/JSManager.cpp` | +90/-0 | 五个无参数 QuickJS API |
| `manager/native/src/Manager/JSManager.hpp` | +26/-0 | JS bridge 声明 |
| `manager/native/src/Manager/Manager.hpp` | +47/-0 | snapshot/operation/service 声明 |
| `manager/native/src/embedded_payload_unprovisioned.cpp` | +8/-0 | 开发失败关闭 payload |
| `manager/package.json` | +24/-0 | manager AMR identity/build scripts |
| `manager/pnpm-lock.yaml` | +10304/-0 | 固定 Falcon JS 依赖 |
| `manager/pnpm-workspace.yaml` | +6/-0 | pnpm workspace |
| `manager/src/app.js` | +14/-0 | Falcon app lifecycle |
| `manager/src/app.json` | +12/-0 | manager page routing |
| `manager/src/base-page.js` | +51/-0 | timer resource cleanup |
| `manager/src/pages/index/index.js` | +11/-0 | page adapter |
| `manager/src/pages/index/index.vue` | +235/-0 | 四操作 UI 与移除二次确认 |
| `manager/src/services/manager-state.js` | +41/-0 | strict snapshot normalization |
| `manager/src/services/manager.js` | +13/-0 | fixed operation facade |
| `manager/test/manager-source.test.js` | +88/-0 | manager 安全源码约束 |
| `manager/test/manager-state.test.js` | +47/-0 | snapshot/operation tests |
| `manager/tools/build-native.sh` | +134/-0 | 全 provisioning 生产构建 |
| `scripts/build_manager.ps1` | +79/-0 | WSL/native/AMR 构建入口 |
| `scripts/install_manager.ps1` | +39/-0 | manager-only AMR 安装 |
| `scripts/uninstall_manager.ps1` | +27/-0 | manager-only AMR 卸载 |

### 影响范围

- **API**: 新增 `Manager.inspect/install/repair/upgrade/remove`。
- **跨模块**: platform package/trust/storage 全链路。
- **数据模型**: `ManagerSnapshot`。
- **配置**: 官方 key、profile、machine、identity、payload 全部必需。

### 回滚指南

- 回滚：`git revert 1710ecc`
- 检查：整个 `manager/` 和 manager scripts。
- 副作用：失去平台安装、修复、升级和安全移除能力。

## [2026-08-24] 保持 Falcon 存活的持久 hole launcher

**类型**: feat  
**提交**: 81116fe  
**风险**: HIGH

### 变更文件

| 文件 | 变更 | 说明 |
|---|---:|---|
| `launcher/api-mock/lvgl_launcher.js` | +19/-7 | status/touch mock |
| `launcher/device/lvgl-supervisor.sh` | +0/-297 | 删除杀进程式旧 supervisor |
| `launcher/device/manifest.template.env` | +0/-6 | 删除旧未签名 runtime manifest |
| `launcher/native/src/Launcher/JSLauncher.cpp` | +51/-2 | status/sendTouch JS bridge |
| `launcher/native/src/Launcher/JSLauncher.hpp` | +1/-0 | touch bridge 声明 |
| `launcher/native/src/Launcher/Launcher.cpp` | +269/-84 | 固定 sessiond 启动、状态和认证触控桥 |
| `launcher/native/src/Launcher/Launcher.hpp` | +28/-7 | status/touch native model |
| `launcher/package.json` | +3/-2 | launcher version/description |
| `launcher/src/pages/index/index.vue` | +305/-86 | 持久 hole 与透明 touch overlay |
| `launcher/src/services/launcher.js` | +9/-2 | normalized status/touch facade |
| `launcher/test/launcher-source.test.js` | +84/-0 | 无 termination path 等约束 |
| `launcher/tools/build-native.sh` | +26/-4 | AArch64 plugin 构建检查 |
| `scripts/device_app_common.ps1` | +0/-58 | 删除旧 shared destructive helper |
| `scripts/device_run_poc.sh` | +0/-150 | 删除旧设备 runner |
| `scripts/install_device_app.ps1` | +17/-101 | 收紧为 launcher-only AMR 安装 |
| `scripts/run_m5.ps1` | +0/-115 | 删除破坏性旧运行入口 |
| `scripts/status_device_app.ps1` | +10/-20 | 改为只读状态收集 |
| `scripts/tests/test_supervisor.ps1` | +0/-119 | 删除旧 supervisor 测试 |
| `scripts/uninstall_device_app.ps1` | +7/-36 | 收紧为 launcher-only AMR 卸载 |

### 影响范围

- **API**: launcher 新增 status/sendTouch，start 改为 hole session。
- **跨模块**: sessiond/touch router。
- **数据模型**: nonce/sequence touch frame 和 launcher status。
- **配置**: 要求 certified hole profile。

### 回滚指南

- 回滚：`git revert 81116fe`
- 检查：launcher native/page 及已删除 supervisor/scripts。
- 副作用：可能恢复会终止 Falcon/miniapp 的旧监督方式，禁止直接用于生产。
