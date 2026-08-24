# 2048 Pitfalls

1. UI 动画不能反向修改 board；模型先原子完成 move，动画只是呈现。
2. 无效滑动不能 spawn，也不能消费 undo。
3. 一个动画期间最多保留一个最新 queued move，避免无界输入队列。
4. restart 必须确认并保留 best score；undo 是单次而非历史栈。
5. reduced-motion 仍要执行完全相同的模型步骤和最终 render。
6. 每个 source tile 都有 motion；两个 merge source 必须收敛到同一 destination，最终 board 只在 motion 完成后显示。
7. 持久化只用 `AppStorage` 固定记录；不得读取环境 FD、拼接 `/userdisk` 或把 CRC32 当作恶意 root 下的认证。
8. `destroy` 前先保存，再删除 motion/score animations；完成回调不得继续引用已删除 overlay tile。
