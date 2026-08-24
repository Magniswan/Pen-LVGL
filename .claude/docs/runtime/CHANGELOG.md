# Changelog - Device Runtime

> 最新变更在最上方。

## [2026-08-24] 独立监督前台应用

**类型**: feat  
**提交**: e1238a5  
**风险**: HIGH

### 变更文件

| 文件 | 变更 | 说明 |
|---|---:|---|
| `platform/include/lvgl_platform/session_control.h` | +13/-2 | 控制 command/payload schema |
| `platform/src/session/session_daemon.cpp` | +176/-91 | desktop/app 切换、READY 等待、精确 child 监督 |
| `platform/src/session/session_control.cpp` | +49/-3 | 128-byte ready/exit/launch/home 协议 |
| `src/runtime/app_control.cpp` | +31/-9 | 应用侧 launch/home 请求 |
| `src/runtime/app_control.h` | +3/-0 | 公共控制方法 |
| `src/session/app_registry.cpp` | +2/-3 | 内置 app identity 调整 |
| `src/session/app_registry.h` | +1/-2 | registry enum 调整 |
| `tests/host/platform_contract_test.cpp` | +14/-1 | control round-trip 与 rejection tests |
| `tests/host/sessiond-source.test.js` | +10/-0 | 禁止 broad kill 并验证独立应用路径 |

### 影响范围

- **API**: `AppControl` 新增 `launch()` 与 `home()`。
- **跨模块**: sessiond、desktop、2048 和所有未来前台应用。
- **数据模型**: control command 增加 canonical app ID。
- **配置**: 无。

### 回滚指南

- 回滚：`git revert e1238a5`
- 检查：session daemon/control 与 app control。
- 副作用：应用将失去独立进程监督和 desktop 返回协议。

## [2026-08-24] 新增失败关闭的 Falcon hole sessiond

**类型**: feat  
**提交**: 092e7a6  
**风险**: HIGH

### 变更文件

| 文件 | 变更 | 说明 |
|---|---:|---|
| `CMakeLists.txt` | +10/-6 | sessiond target 替换旧入口 |
| `platform/include/lvgl_platform/session_control.h` | +25/-0 | child READY/EXIT 协议 |
| `platform/src/session/session_daemon.cpp` | +637/-0 | 固定平台复验、hole、触控和 child 生命周期 |
| `src/session/session_main.cpp` | +0/-169 | 删除旧会话入口 |
| `platform/src/session/session_control.cpp` | +62/-0 | 原生 child 控制通道 |
| `tests/host/platform_contract_test.cpp` | +13/-0 | session control 契约测试 |
| `tests/host/sessiond-source.test.js` | +33/-0 | sessiond 安全源码约束 |

### 影响范围

- **API**: 引入 session control。
- **跨模块**: Falcon launcher 和所有原生应用。
- **数据模型**: session status/control 状态机。
- **配置**: sessiond 只接受签名 certified profile。

### 回滚指南

- 回滚：`git revert 092e7a6`
- 检查：session daemon、control 和旧 `session_main.cpp`。
- 副作用：会恢复旧入口并失去 hole fail-closed 会话。
