# 2048 Model API

## `Game2048`

```cpp
explicit Game2048(std::uint64_t seed);
MoveOutcome move(Direction direction);
bool undo();
void restart();
```

模型不依赖 LVGL。构造时使用非零 deterministic seed；UI 只通过只读 board/score/best/phase accessors 渲染。`move` 在无变化时不 spawn、不计分、不覆盖 undo；有效移动保存完整前态后产生一个新 tile。

## `MoveOutcome`

- `changed`：board 是否改变。
- `score_delta`：本次合并得分。
- `spawned` / spawn row/column/exponent：新 tile 事件。
- `motions`：预留的逐 tile motion；当前实现尚未填充。
- phase transition：由模型在 move 完成后计算 won/lost。

UI 必须以模型输出为事实，不在动画回调里重新推导合并规则。queued gesture 只保留一个，并在当前动画结束后再次调用 `move`。

## Persistence boundary

当前没有公共持久化接口。后续应由平台提供 app-private storage，把 board、score、best、phase、RNG state、undo snapshot 与 schema/version 封装为有界 canonical record；模型本身仍不直接访问路径。
