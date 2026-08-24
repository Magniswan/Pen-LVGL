# Changelog - SDK / Shell

> 最新变更在最上方。

## [2026-08-24] 新增有界原子 AppStorage API

**类型**: feat
**提交**: 0f196ed
**风险**: HIGH

### 变更文件

| 文件 | 变更 | 说明 |
|---|---:|---|
| `src/runtime/app_storage.cpp` | +189/-0 | record 校验、bounded read 与原子 write |
| `src/runtime/app_storage.h` | +29/-0 | 不可复制的最小 SDK 接口 |
| `CMakeLists.txt` | +27/-1 | runtime linkage 与 host visual target |

### 影响范围

- **API**: 新增 `available/read/write_atomic`；`RuntimeContext` 布局变化。
- **数据模型**: record 名最长 64 bytes，闭合 ASCII 字符集，应用自带 schema/version/checksum。
- **兼容性**: SDK ABI 字符串仍为 `1.0`，但当前尚未承诺稳定二进制 ABI，必须全量重编。

### 回滚指南

- 回滚：`git revert 0f196ed`
- 检查：`app_control` target、runtime context 与使用 storage 的应用。
- 副作用：应用私有状态接口和 2048 恢复失效。

## [2026-08-24] 支持 stable ID 启动与返回桌面

**类型**: feat  
**提交**: e1238a5  
**风险**: HIGH

### 变更文件

| 文件 | 变更 | 说明 |
|---|---:|---|
| `src/runtime/app_control.cpp` | +31/-9 | 编码 launch/home 控制请求 |
| `src/runtime/app_control.h` | +3/-0 | 新增公共 SDK 方法 |
| `platform/include/lvgl_platform/session_control.h` | +13/-2 | 扩展固定控制协议 |
| `tests/host/platform_contract_test.cpp` | +14/-1 | canonical ID 与协议往返测试 |

### 影响范围

- **API**: `AppControl::launch(stable_id)`、`AppControl::home()`。
- **跨模块**: desktop 与所有应用入口。
- **数据模型**: command payload 现在携带 canonical app ID。
- **配置**: 无。

### 回滚指南

- 回滚：`git revert e1238a5`
- 检查：`src/runtime/app_control.*` 与 session control header。
- 副作用：SDK app 无法请求切换或返回桌面。
