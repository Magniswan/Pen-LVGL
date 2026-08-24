# Offline Release Process

## Roles

- Builder：无私钥，生成可复现 `.lvapp.dev`、SBOM/hash/test report。
- Reviewer：核对 source、manifest、counter/epoch、target matrix 和 test evidence。
- Offline signer：隔离环境持有官方私钥，只签已批准 digest。
- Publisher：只分发已独立验签的最终 bytes，不接触私钥。

至少使用双人审批；signer 主机不连接设备、源码开发环境或公网。

## Application release

1. 冻结 source commit、SDK ABI、tool versions 和 manifest。
2. 运行 host tests、AArch64 build、ELF dependency audit。
3. 两次独立 clean build，比较 `.lvapp.dev` bytes。
4. reviewer 分配唯一单调 `releaseCounter`，确认 `securityEpoch`。
5. 把 dev artifact 和批准 digest 送入 offline signer。
6. 执行 `lvapp sign`；立即用独立 public key 环境执行 `lvapp verify`。
7. 记录最终 SHA-256、signer key ID、app/version/counter/epoch、commit 与测试报告。
8. 只把最终 `.lvapp` 放入 root-owned fixed inbox；设备 installer 仍会独立重验。

## Platform/manager release

1. 真机认证 `profile.env`，记录 connector/CRTC/overlay/rotation/touch evidence。
2. 以 `top.lvgl.platform` manifest 打包完整 platform release 并离线签名。
3. 用 production 参数构建 manager native plugin，嵌入同一 signed platform bytes、official public key、profile/machine 和 device identity digest。
4. 构建 manager/launcher AMR，记录 AMR SHA-256。
5. 在隔离的认证测试笔上执行 install/repair/upgrade/remove/crash/reboot/tamper/rollback matrix。
6. 只有全门禁通过才发布 AMR；launcher 与 manager AppID 固定且相互独立。

## Fail-closed gates

以下任一发生就停止发布：缺官方 public key/payload/identity；profile 未认证；dev flag；测试 key；counter 冲突；非可复现 bytes；ELF ABI/NEEDED 不匹配；测试失败；signer/verifier key ID 不一致。

## Private key rules

- 不提交、不复制到 CI secret、不作为 CMake/PowerShell 参数传给普通构建。
- signer audit 只记录 key ID，不打印 PEM 或原始 key。
- 备份使用离线加密介质/硬件设备和双人恢复。
- 怀疑泄露立即执行 [SECURITY.md](../../SECURITY.md) 的 key compromise response。

## Current blocker

仓库当前没有官方私钥和正式 signed platform payload，因此只能生成 `.lvapp.dev` 验证产物；任何“正式包”交付都必须等待合法 offline signer provisioning。
