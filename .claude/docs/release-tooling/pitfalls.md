# Release Tooling Pitfalls

## 公钥可分发，私钥不可构建注入

官方公钥需要编译进所有 verifier；私钥只存在于离线 signer。不要为了自动化把私钥改成 CMake、manager 或 CI 环境变量。

## `.lvapp.dev` 不是候选正式包

dev 包故意无签名且 signing key ID 为零，设备端永远拒绝。扩展名不是安全边界，格式 flag 才是；改名不能变成正式包。

## 签名后不可重打包

签名覆盖 header、manifest、table 和 payload。任何 metadata、排列、padding 或字节变化都会失效；发布记录以最终 `.lvapp` hash 为准。

## Counter 需要中央分配

离线 v1 仍需要维护每 app 单调 `releaseCounter` 和必要时递增的 `securityEpoch`。重复/降低 counter 会被设备 high-water 拒绝。未来在线 entitlement/revocation 仅进入 TODO，不改变 v1 的官方签名要求。

## ADB 脚本仍需加强目标认证

当前脚本检查单设备、ABI 和 AppID owner，但尚无 serial allowlist/硬件 identity gate。连接的 Nexus 4 绝不能用于本项目的安装测试；正式发布前必须补上目标身份门禁。
