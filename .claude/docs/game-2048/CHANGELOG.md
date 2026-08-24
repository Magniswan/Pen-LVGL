# Changelog - 2048

> 最新变更在最上方。

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
