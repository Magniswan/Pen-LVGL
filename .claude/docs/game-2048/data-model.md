# 2048 Data Model

- `board[16]`：row-major，0 表示空，否则 2 的幂。
- `score`：本局合并值累计；`best_score >= score`。
- `GamePhase`：playing / won / lost。出现 ≥2048 即 won；满盘且无邻接相同值为 lost。
- `MoveOutcome`：changed、score delta、最多 16 个 source→destination motions、spawn cell/value。
- `Snapshot`：有效 move 前的 board、score、phase、PRNG state；仅保留一次 undo。
- `PersistentGameState`：当前模型与完整 undo snapshot；反序列化时连 inactive undo 数据也必须合法。
- `state.v1`：128-byte little-endian wire record；CRC32 用于损坏检测，不是对 root 攻击者的认证。

`collapse_line` 先压缩再成对合并，新生成 tile 在同一 move 中不得二次合并。无变化 move 不计分、不 spawn、不覆盖 undo。

## 测试覆盖

- 空隙压缩与单次合并；
- `2,2,4,4` 与四相同 tile；
- 多行计分；
- undo 恢复；
- lost/won；
- 相同 seed 初始状态一致。
- 左/右物理 motion 与双源合并目标；
- persistence round-trip、CRC tamper、非法 tile 与 inactive undo 拒绝；
- 960×266 离屏首帧非空和颜色丰富度。
