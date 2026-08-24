# Roadmap / Deferred Work

此文件记录后续代办，不把未完成能力描述成当前功能。

## P0 — 商用安全门禁

- 为每个 app 配置独立非 root UID/GID；落地 `setgroups/setresgid/setresuid`。
- 按 manifest 强制 rlimit、seccomp allowlist、cgroup CPU/memory 与只读 mount namespace。
- 实现 sessiond broker 的 app-private storage FD API、原子记录、quota 和跨 app 隔离。
- 增加应用卸载/隔离/rollback UI；保留 anti-rollback policy 和审计记录。
- ADB 脚本增加 serial allowlist + signed device identity gate。
- 为 package/state/session parser 加 coverage-guided fuzzing 与 sanitizer CI。
- 平台/app release 安装与启动路径完成独立安全审计。

## P0 — 硬件认证

- 在目标 Y01 firmware 批次测量并认证 DRM overlay、zpos、rectangle、rotation、touch 和 crash recovery。
- 认证 profile 生成签名 `profile.env`；此前保持 `HOLE_SESSION_CERTIFIED=0`。
- 确认 secure boot/verified boot/dm-verity/TEE/IMA 可用性，形成真实 root 防护声明。
- 完成 cold boot、upgrade power loss、storage corruption、tamper 和 rollback matrix。

## P1 — SDK 与应用体验

- 稳定 SDK ABI header/export package，加入 ABI compatibility CI。
- 2048 接入 private storage，持久化 best/board/RNG/undo。
- 2048 填充精确 per-tile motion，加入 merge pulse、spawn 与 reduced-motion golden tests。
- 提供 host LVGL simulator/screenshot harness 和 960×266 golden image regression。
- 完成 capability broker（audio/dictionary/haptics/scanner/network）。
- 增加应用图标/本地化 metadata 的签名格式与 desktop cache。

## P1 — 发布运维

- 使用 HSM/硬件 token 的离线 signer、双人审批和透明 audit log。
- key rotation/recovery drill、security epoch 响应手册和 reproducible release attestations。
- SBOM、license、CVE scanning、dependency pinning 与长期支持分支。

## 暂缓：在线能力

按当前需求，v1 保持完全离线。后续可能加入 entitlement、在线吊销、透明日志和更新元数据，但现在不实现。任何在线授权都只能增加约束，不能替代官方包签名、设备本地验签和 anti-rollback。
