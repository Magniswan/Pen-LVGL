# Runtime Pitfalls

1. 不要把 CloudBrowser 的 plane ID 直接写成“已认证”；必须在目标固件捕获证据并更新签名 profile。
2. 不要调用 `drmModeSetCrtc`、更换 mode 或关闭 Falcon plane；hole 的目标是共存。
3. READY 必须在首个成功 KMS present 后发送，UI 对象创建完成不代表用户可见。
4. 前台切换时不要 reset 外部 touch sequence；Falcon 序列在整个 session 内连续。
5. 发起 launch/home 的手势应已结束，避免把未结束 contact 交给下一应用。
6. 不要把 app ID 拼成可执行路径；用已验证 manifest entry 和目录 FD。
7. 不要让普通应用继承 registry、安装目录或不相关 FD；child 继承集合必须显式。
8. 不要放宽 seccomp 以修复未知崩溃；先在认证固件捕获所需 syscall，并逐项证明最小参数约束。现有 UID/GID、rlimit、seccomp 仍需 cgroup/namespace 和真机认证补全。
9. 不要对非目标设备运行设备脚本；当前 Nexus 4 明确不在授权范围。
