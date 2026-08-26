# 2048 Reference Application

## 概述

`apps/game-2048` 是 SDK 参考应用：确定性纯 C++ 模型与 LVGL UI 分离，适配 960×266 横屏，提供滑动、合并计分、胜负、单步撤销、重开确认、reduced-motion 和矿物仪器视觉。

它只包含 `sdk/include/lvgl_platform/*.hpp` 公开面，因此同时充当 SDK `1.0` 的真实消费示例。

## 依赖

- [SDK / Shell](../sdk-shell/README.md)
- [Device Runtime](../runtime/README.md)
- LVGL 与 Montserrat/应用中文字体

## 流程

```text
gesture -> request_move -> Game2048::move (truth)
                         -> MoveOutcome
                         -> exact source-to-destination tile animation
                         -> merge pulse + spawn + score effect
                         -> atomic private-state save
                         -> one queued gesture after completion
```

模型使用 xorshift64*，相同 seed 与操作序列得到相同 spawn。每次有效 move 保存移动前 board/score/phase/random state，因此 undo 也恢复确定性随机序列。

## 当前完成度

- 模型规则与 host tests 完成。
- 入场、精确逐 tile 位移、merge pulse、spawn overshoot 与 score 动画完成。
- reduced-motion 直接提交模型终态，不改变游戏规则。
- board/score/best/phase/RNG/单步 undo 以 128-byte canonical record 原子持久化。
- host model tests 与 960×266 离屏视觉回归完成；固定 initial/motion/resolution/terminal digest、16 格边界和 reduced-motion 终态一致性，真机手势/性能认证仍在 roadmap。
- 可生成开发 `.lvapp.dev`；正式包必须由官方离线密钥签名。

## 详细文档

- [api-model.md](api-model.md)：模型与 UI 的稳定边界。
- [data-model.md](data-model.md)：board、outcome、undo 与 phase。
- [pitfalls.md](pitfalls.md)：规则、动画和持久化注意事项。
- [CHANGELOG.md](CHANGELOG.md)：变更与验证。
