# Changelog - Configuration

> 最新变更在最上方。

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
