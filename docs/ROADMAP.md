# Roadmap / Deferred Work

此文件记录后续代办，不把未完成能力描述成当前功能。

## P0 — 商用安全门禁

- 在认证目标内核上增加并验证 cgroup v2 CPU/memory 与只读 mount namespace；不支持所需内核能力时保持失败关闭。
- 对 AArch64 seccomp 白名单做目标固件 syscall capture、负向逃逸测试和独立审计；当前实现已强制 UID/GID、rlimit、no-new-privs 与 seccomp，但尚无真机证据。
- 为 storage broker 增加断电/磁盘满/并发恶意请求集成测试；当前已强制 manifest 的 `maxFiles`/`dataMiB` quota。
- 把现有 ADB `-Serial` + identity digest 防误操作门禁升级为“官方签名的 release authorization”；当前显式 digest 尚不是硬件 attestation，也不能抵御能伪造 ADB 响应的 root 对手。
- 运行并持续运营新增的 package/state/session libFuzzer + ASan/UBSan CI：首次远端 workflow 尚待执行；后续加入长期 corpus、覆盖率阈值、定期长跑、crash 去重和修复 SLA。
- 平台/app release 安装与启动路径完成独立安全审计。

## P0 — 硬件认证

- 在目标 Y01 firmware 批次测量并认证 DRM overlay、zpos、rectangle、rotation、touch 和 crash recovery。
- 认证 profile 生成签名 `profile.env`；此前保持 `HOLE_SESSION_CERTIFIED=0`。
- 确认 secure boot/verified boot/dm-verity/TEE/IMA 可用性，形成真实 root 防护声明。
- 完成 cold boot、upgrade power loss、storage corruption、tamper 和 rollback matrix。

## P1 — SDK 与应用体验

- 为已发布的 SDK `1.0` headers/export component 加入跨版本 ABI compatibility CI 与弃用策略。
- 2048 已有确定性 initial/motion/resolution/terminal golden digest、棋盘边界检查和 reduced-motion 终态一致性；仍需在认证硬件完成手势与帧时间基线。
- 完成 capability broker（audio/dictionary/haptics/scanner/network）。
- 增加应用图标/本地化 metadata 的签名格式与 desktop cache。
- 为 storage broker 增加删除/列举等经过审核的最小 API；v1 仍只有固定记录 read/atomic-write。

## P1 — 发布运维

- 仓库已提供 HSM 公钥 challenge/proof-of-possession、双 witness 与 trust-root bundle 流程；仍需在真实 HSM 上执行生产 ceremony，并接入组织的 WORM/透明 audit log。
- 在隔离测试 HSM 上完成 key rotation/recovery drill；v1 单 trust slot 的现场替换仍依赖经认证 platform/manager 渠道。
- 已生成 SPDX 2.3 SBOM、license notices、构建证据和双摘要；后续增加 CVE scanning、签名 attestation、依赖镜像/来源证明与长期支持分支。

## 暂缓：在线能力

按当前需求，v1 保持完全离线。后续可能加入 entitlement、在线吊销、透明日志和更新元数据，但现在不实现。任何在线授权都只能增加约束，不能替代官方包签名、设备本地验签和 anti-rollback。

## 已落地但仍待硬件验证

- installer broker v2 已提供已安装枚举、手动 rollback 和 payload remove；UI 对两项破坏性操作强制二次确认。
- rollback 会重验 official previous、隔离 current、保留 high-water；remove 使用随机 tombstone 与有界 no-follow 清理，只保留 policy/data。
- BEGIN/COMMIT/ISOLATED 审计记录已持久化且有大小上限；真实性仍依赖后续可信启动、TEE 或远端透明日志。
