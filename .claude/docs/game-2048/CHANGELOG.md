# Changelog - 2048

> 最新变更在最上方。

## [2026-08-26] 迁移到公开 SDK 1.0

**类型**: refactor
**提交**: 14abc6b
**风险**: LOW

- `main.cpp`、UI 与 persistence 改为只包含 `lvgl_platform/*.hpp`。
- 模型/存档格式/动画行为未改变；public header compile test 与 AArch64 2048 build 通过。
- 回滚需与 SDK header 发布一并执行：`git revert 14abc6b`。

## [2026-08-24] 完成逐 tile 动画与原子恢复

**类型**: feat
**提交**: 0f196ed
**风险**: MEDIUM

### 变更文件

| 文件 | 变更 | 说明 |
|---|---:|---|
| `apps/game-2048/game_2048_model.cpp` | +85/-14 | 物理 motion、持久态验证与恢复 |
| `apps/game-2048/game_2048_persistence.cpp` | +126/-0 | 128-byte canonical record 与 CRC32 |
| `apps/game-2048/game_2048_ui.cpp` | +257/-51 | 逐 tile、merge、spawn、score 动画与保存时机 |
| `tests/host/game_2048_model_test.cpp` | +45/-0 | motion、round-trip 与 tamper tests |
| `tests/host/game_2048_visual_test.cpp` | +86/-0 | 960×266 离屏视觉烟测 |

### 影响范围

- **API**: 内部模型新增 `persistent_state()` / `restore_state()`；UI 接收 `AppStorage`。
- **数据模型**: 新增 versioned `state.v1`，包含 board/score/best/phase/RNG/undo。
- **动画**: source tile 精确汇聚到 destination，再提交模型终态和 resolution effects。
- **安全**: 路径不可由游戏指定；CRC32 只用于损坏检测。

### 回滚指南

- 回滚：`git revert 0f196ed`
- 检查：同时回滚 runtime storage/sessiond 协议，避免 `RuntimeContext` ABI 不一致。
- 副作用：会移除 2048 恢复、精确 motion 和视觉烟测。

## [2026-08-24] 新增矿物主题动画界面

**类型**: feat  
**提交**: 7b22542  
**风险**: MEDIUM

### 变更文件

| 文件 | 变更 | 说明 |
|---|---:|---|
| `apps/game-2048/game_2048_ui.cpp` | +339/-0 | 棋盘、手势、矿物视觉和动画状态机 |
| `apps/game-2048/game_2048_ui.h` | +55/-0 | UI 对象与动画状态 |
| `apps/game-2048/main.cpp` | +48/-0 | RuntimeApplication 入口 |
| `CMakeLists.txt` | +13/-0 | 新增 AArch64 `lvgl-2048` target |

### 影响范围

- **API**: 无外部 API。
- **跨模块**: 依赖 platform runtime 与 app shell。
- **数据模型**: UI 消费 `MoveOutcome`，未改模型规则。
- **配置**: 新增构建目标。

## [2026-08-24] 新增确定性 2048 模型

**类型**: feat  
**提交**: 2732867  
**风险**: MEDIUM

### 变更文件

| 文件 | 变更 | 说明 |
|---|---:|---|
| `apps/game-2048/game_2048_model.cpp` | +168/-0 | move/merge/spawn/undo 和 xorshift64* |
| `apps/game-2048/game_2048_model.h` | +77/-0 | board、phase、outcome 数据模型 |
| `tests/host/game_2048_model_test.cpp` | +62/-0 | 合并、计分、胜负与撤销测试 |
| `CMakeLists.txt` | +14/-0 | 新增 host model test |

### 影响范围

- **API**: 新增内部 `Game2048` 模型接口。
- **跨模块**: 仅参考应用与 host tests。
- **数据模型**: 4×4 exponent board、undo snapshot、seeded RNG。
- **配置**: 无。
