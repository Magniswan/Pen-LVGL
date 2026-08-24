# 2048 Pitfalls

1. UI 动画不能反向修改 board；模型先原子完成 move，动画只是呈现。
2. 无效滑动不能 spawn，也不能消费 undo。
3. 一个动画期间最多保留一个最新 queued move，避免无界输入队列。
4. restart 必须确认并保留 best score；undo 是单次而非历史栈。
5. reduced-motion 仍要执行完全相同的模型步骤和最终 render。
6. 当前整板 slide 不是最终 per-tile 动画；不要把 `motions` 为空误认为没有 tile 位移。
7. 持久化必须等待 SDK 私有原子存储，不能直接写共享绝对路径。
