# Release Tooling Pitfalls

## 公钥可分发，私钥不可构建注入

官方公钥需要编译进所有 verifier；私钥只存在于离线 signer。不要为了自动化把私钥改成 CMake、manager 或 CI 环境变量。

## `.lvapp.dev` 不是候选正式包

dev 包故意无签名且 signing key ID 为零，设备端永远拒绝。扩展名不是安全边界，格式 flag 才是；改名不能变成正式包。

## 签名后不可重打包

签名覆盖 header、manifest、table 和 payload。任何 metadata、排列、padding 或字节变化都会失效；发布记录以最终 `.lvapp` hash 为准。

## Counter 需要中央分配

离线 v1 仍需要维护每 app 单调 `releaseCounter` 和必要时递增的 `securityEpoch`。重复/降低 counter 会被设备 high-water 拒绝。未来在线 entitlement/revocation 仅进入 TODO，不改变 v1 的官方签名要求。

## ADB identity 是防误操作门禁，不是 attestation

当前脚本拒绝隐式目标，要求精确 serial 和 64 位非零 identity digest，并把每条业务命令 pin 到该 serial。不要把调用者传入的 digest 当作官方授权：root 设备可伪造 ADB 响应，恶意操作员也可传入另一个摘要。生产 manager 必须把认证 identity 与官方签名 payload 编译绑定；后续仍需官方签名 release authorization/硬件 attestation。连接的 Nexus 4 绝不能作为目标。

## 不要在普通构建节点“顺手签名”

`stage_platform_release.ps1` 故意没有私钥参数；`sign_release.ps1` 也要求私钥路径位于仓库外，并把待签 SHA-512 与事先批准值绑定。不要合并这两个角色、从待签包自动接受 key、覆盖已有输出，或把 signer 私钥放进 CI secret。

## clean Git 不等于可复现证明

stager 将 commit/timestamp 和构建证据绑定到输出，但正式门禁仍要两个独立 clean build 逐字节比较。SBOM/notices 也不等于 CVE 扫描、来源 attestation 或 license 法律审查；这些后续项保留在 ROADMAP。

## AMR 内层 MD5 不是平台信任根

Falcon manifest 的 size/MD5 用于发现归档损坏和工具链漂移；官方 LVGL 平台/应用授权仍由 Ed25519 `.lvapp`、嵌入公钥和设备侧复验完成。不能把 AMR MD5 描述为抗 root 签名。
