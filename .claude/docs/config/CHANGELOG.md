# Changelog - Configuration

> 最新变更在最上方。

## [2026-08-26] 固定 Falcon 发布工具链

**类型**: security
**提交**: 16d557f
**风险**: MEDIUM

- launcher/manager 均要求 Node 18.20.8 和 `aiot-vue-cli 1.0.32`。
- native bridge 固定 AArch64 工具链，AMR 按 production/development 模式检查脚本布局。
- 回滚：`git revert 16d557f`；副作用是失去可复现工具链门禁。

## [2026-08-26] 把认证配置绑定到 release staging

**类型**: security
**提交**: 381027e
**风险**: HIGH

- stager 只接受 LF/UTF-8、精确 16 字段、`HOLE_SESSION_CERTIFIED=1` 的 profile，并把原始 `profile.env` 纳入待签平台包。
- production CMake cache 必须是 Release、production mode 且绑定同一官方 public key。
- 回滚：`git revert 381027e`；会失去可执行的配置/产物交接门禁。

## [2026-08-24] 要求认证 overlay session profile

**类型**: feat  
**提交**: 64b31a9  
**风险**: HIGH

### 变更文件

| 文件 | 变更 | 说明 |
|---|---:|---|
| `platform/src/session/session_profile.cpp` | +140/-0 | canonical profile.env 解析与严格值校验 |
| `platform/include/lvgl_platform/session_profile.h` | +42/-0 | session profile schema 类型 |
| `tests/host/platform_contract_test.cpp` | +31/-0 | profile 编码/字段/矩形测试 |
| `CMakeLists.txt` | +1/-0 | 将 profile parser 纳入 contract library |

### 影响范围

- **API**: 新增 session profile parser。
- **跨模块**: launcher、sessiond、DRM/input runtime。
- **数据模型**: signed `profile.env` 成为会话设备配置。
- **配置**: `HOLE_SESSION_CERTIFIED=1` 和 overlay 参数强制有效。

### 回滚指南

- 回滚：`git revert 64b31a9`
- 检查：session profile header/source 与平台包 profile.env。
- 副作用：会话可能失去认证 profile 的失败关闭边界。
