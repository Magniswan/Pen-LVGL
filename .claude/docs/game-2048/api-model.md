# 2048 Model API

## `Game2048`

```cpp
explicit Game2048(std::uint64_t seed);
MoveOutcome move(MoveDirection direction);
bool undo();
void reset();
PersistentGameState persistent_state() const;
bool restore_state(const PersistentGameState& state);
```

模型不依赖 LVGL。构造时使用非零 deterministic seed；UI 只通过只读 board/score/best/phase accessors 渲染。`move` 在无变化时不 spawn、不计分、不覆盖 undo；有效移动保存完整前态后产生一个新 tile。

## `MoveOutcome`

- `changed`：board 是否改变。
- `score_delta`：本次合并得分。
- `spawned_cell` / `spawned_value`：新 tile 事件。
- `motions`：每个非空 source tile 的物理 from/to cell、原值和 merged 标记；两个合并源指向同一 destination。
- phase transition：由模型在 move 完成后计算 won/lost。

UI 必须以模型输出为事实，不在动画回调里重新推导合并规则。queued gesture 只保留一个，并在当前动画结束后再次调用 `move`。

## Persistence boundary

`persistent_state` 导出 board、score、best、phase、RNG state 与 undo snapshot；`restore_state` 会验证 tile、phase、score 和 RNG 不变量后才原子替换模型。`game_2048_persistence` 把它编码为固定 128-byte、little-endian、带 magic/version/CRC32 的 canonical record，并只通过 `AppStorage` 的 `state.v1` 记录访问。模型和 persistence 层都不接受路径。
