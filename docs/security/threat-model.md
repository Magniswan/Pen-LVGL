# Security Threat Model

## Security objective

生产设备只安装和启动由官方离线发布密钥签名、适配当前平台/profile/machine/ABI、未回滚且磁盘字节完整的 LVGL 平台和应用。UI、文件名、调用者路径、环境变量和第三方 key 均不构成授权。

## Adversaries

| 能力 | 当前防护 |
|---|---|
| 修改/替换 inbox 包 | 完整 SHA-512 token、安装前重读重验、Ed25519 |
| symlink/hardlink/path traversal | dirfd + `O_NOFOLLOW`、nlink=1、canonical path、固定 roots |
| 降级到旧官方版本 | app-specific counter/epoch/digest 双槽 high-water |
| 修改已安装字节/mode/owner | 每次启动前逐文件复验并从 verified FD 执行 |
| 伪造 desktop registry/launch path | sessiond-owned canonical registry；desktop 只传 app ID |
| 触控重放/乱序/越界 | per-session nonce、strict sequence、monotonic age、contact lifecycle |
| 利用 broad process kill | 只监督 exact child PID；源码测试禁止 broad kill |
| patch 普通用户态文件 | owner/mode/hash/signature 检查，失败关闭 |
| 已控制 root/内核 | 无法仅靠用户态彻底防御；见下方边界 |

## Root boundary

root 可改内核、`ptrace`/写内存、替换动态链接器、拦截系统调用或直接 patch verifier。若 boot chain 不验证内核和 rootfs，攻击者也能让“验签成功”变成恒真。因此本仓库不宣称在任意 root 下不可破解。

商用防 root 的完整闭环需要：

1. hardware root of trust + locked bootloader；
2. verified boot/dm-verity，覆盖内核、init、动态链接器和平台 verifier；
3. TEE/secure element 保存设备身份和 monotonic state；
4. IMA appraisal 或等价 executable measurement；
5. 最小权限 UID、seccomp、rlimit/cgroup、只读 mount namespace；
6. 可轮换/吊销的 offline release key hierarchy；
7. 可审计生产构建和独立复现/签名验证。

在这些硬件条件未知时，当前策略是把所有不能可靠证明的 profile/identity/状态视为不可信并拒绝运行。

## Trust roots

- production trust slot 只容纳一个官方 Ed25519 public key。
- all-zero key 和 RFC 8032 test key 被构建系统拒绝。
- dev build 的空 key 导致 verifier/manager/sessiond 不可用于生产安装。
- 设备无导入 key、切换 key、忽略签名或“install anyway”入口。

## Residual risks

- 前台应用当前仍继承 root 身份，尚未完成 UID/seccomp/namespace 强制隔离。
- Y01 hole/overlay profile 尚未真机认证。
- ADB scripts 尚缺 serial allowlist + hardware identity gate。
- 私有存储与 quota enforcement 尚未完成，2048 暂无安全持久化。
- 在线 entitlement/revocation 属于后续阶段；offline v1 不依赖网络。
