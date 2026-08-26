# Offline Release Process

## Roles

- Builder：无私钥，生成可复现 `.lvapp.dev`、SBOM/hash/test report。
- Reviewer：核对 source、manifest、counter/epoch、target matrix 和 test evidence。
- Offline signer：隔离环境持有官方私钥，只签已批准 digest。
- Publisher：只分发已独立验签的最终 bytes，不接触私钥。

至少使用双人审批；signer 主机不连接设备、源码开发环境或公网。

正式 key 首次启用前必须完成 [官方签名密钥仪式](../security/key-ceremony.md)：HSM 内不可导出生成、双 witness 核对、exact challenge 的 Ed25519 proof-of-possession，以及仓库外 trust-root evidence bundle。发布流程只接受该 bundle 中批准的 raw public key；不能从待签包、构建参数或 signer 私钥推导后自动接受 key。

## Application release

1. 冻结 source commit、SDK ABI、tool versions 和 manifest。
2. 运行 host tests、AArch64 build、ELF dependency audit。
3. 两次独立 clean build，比较 `.lvapp.dev` bytes。
4. reviewer 分配唯一单调 `releaseCounter`，确认 `securityEpoch`。
5. 把 dev artifact 和批准 digest 送入 offline signer。
6. 执行 `lvapp sign`；立即用独立 public key 环境执行 `lvapp verify`。
7. 记录最终 SHA-256、signer key ID、app/version/counter/epoch、commit 与测试报告。
8. 只把最终 `.lvapp` 放入 root-owned fixed inbox；设备 installer 仍会独立重验。

应用 signer 可直接使用 `lvapp sign/verify`；平台正式发布必须使用下述脚本，把 builder 与 signer 的输入输出结构固定下来。

## Executable offline handoff

Builder 在 clean worktree、production AArch64 Release build 和已认证 `profile.env` 上运行：

```powershell
./scripts/stage_platform_release.ps1 `
  -BuildDirectory build/platform-production `
  -CertifiedProfileEnv <certified-profile.env> `
  -OutputDirectory <new-staging-directory> `
  -OfficialPublicKeyHex <approved-32-byte-public-key-hex> `
  -Version <semver> `
  -ReleaseCounter <monotonic-counter> `
  -SecurityEpoch <epoch> `
  -WslDistribution <approved-wsl-name>
```

Reviewer 检查 source commit、manifest/profile、ELF evidence、SPDX/notices、inspection 和测试记录，并通过独立渠道批准 `top.lvgl.platform.lvapp.dev` 的 SHA-512。隔离 signer 只接受这一个 digest：

```powershell
./scripts/sign_release.ps1 `
  -DevelopmentPackage <top.lvgl.platform.lvapp.dev> `
  -PrivateKey <repository-external-offline-private.pem> `
  -PublicKey <approved-official-public.pem> `
  -ExpectedPublicKeyHex <approved-32-byte-public-key-hex> `
  -ExpectedDevelopmentSha512 <reviewer-approved-128-hex> `
  -ExpectedSourceCommit <reviewer-approved-40-hex-commit> `
  -OutputDirectory <new-signer-output-directory>
```

stager 会核对 CMake home，从 cache 锁定 CMake 3.31.6/Ninja 1.12.1，并在指定 WSL 中从当前 clean source `--clean-first` 重建目标；构建后 source 有变化也会拒绝。两个脚本均要求 Node 18.20.8。signer 另拒绝非 reviewer-approved/dirty source commit、测试/全零公钥、仓库内私钥、digest/key 不匹配、链接输入与覆盖已有输出。signer 输出仍由 publisher 在另一个 public-key 环境复验。

## Platform/manager release

1. 真机认证 `profile.env`，记录 connector/CRTC/overlay/rotation/touch evidence。
2. 以 `top.lvgl.platform` manifest 打包完整 platform release 并离线签名。
3. 用 production 参数构建 manager native plugin，嵌入同一 signed platform bytes、official public key、profile/machine 和 device identity digest。
4. 用精确 Node 18.20.8 运行 `scripts/build_manager.ps1 -Production` 与 `scripts/build_launcher.ps1 -Production`；两者检查 `aiot-vue-cli 1.0.32`、AArch64 bridge、AMR 精确条目/manifest cert，规范化 ZIP metadata、再次验包并记录可复现 SHA-256。
5. 在隔离的认证测试笔上执行 install/repair/upgrade/remove/crash/reboot/tamper/rollback matrix。
6. 只有全门禁通过才发布 AMR；launcher 与 manager AppID 固定且相互独立。

## Certified device operations

认证阶段记录目标 ADB serial，并计算与 manager 完全相同的 identity：

```text
SHA-256(
  UTF-8(uname.machine) || 00 ||
  raw(/etc/miniapp/resources/local_packages.json) || 00 ||
  raw(/etc/miniapp/resources/cfg.json) || 00
)
```

把该摘要同时用于 production manager 的 `DeviceIdentitySha256Hex` 和 host 脚本的 `-ExpectedIdentitySha256`。脚本要求目标固件提供 `base64`，以不经过文本换行转换的方式读取两份 evidence。示例中的值必须来自认证记录，不能现场从未知设备读取后直接回填：

```powershell
.\scripts\install_manager.ps1 `
  -Serial <certified-adb-serial> `
  -ExpectedIdentitySha256 <certified-64-hex-digest>

.\scripts\install_device_app.ps1 `
  -Serial <certified-adb-serial> `
  -ExpectedIdentitySha256 <certified-64-hex-digest>

.\scripts\status_device_app.ps1 `
  -Serial <certified-adb-serial> `
  -ExpectedIdentitySha256 <certified-64-hex-digest>
```

卸载脚本具有相同两个必填参数，另保留 PowerShell 高风险确认。允许同时连接多个设备，因为实际业务命令始终携带 `-s`；serial 缺失、重复、offline/unauthorized、identity 不同、evidence 缺失或 base64 无效都会在业务操作前失败。

这是操作安全门禁，不是信任根。能伪造 ADB 输出的 root 对手可以冒充摘要；正式安全判断由 manager 内嵌认证 identity、唯一官方 Ed25519 公钥、已签平台包和设备可信启动链共同完成。官方签名的 host release authorization/硬件 attestation 仍在 P0 代办中。

## Fail-closed gates

以下任一发生就停止发布：Git 不干净或 source commit 不符；build cache 不属于当前 source；clean rebuild 失败；缺官方 public key/payload/identity；profile 未认证；dev flag；测试 key；counter 冲突；reviewer SHA-512 不一致；Node 版本不符；非可复现 bytes；ELF ABI/NEEDED 不匹配；AMR 有额外/缺失条目；测试失败；signer/verifier key ID 不一致。

## Private key rules

- 不提交、不复制到 CI secret、不作为 CMake/PowerShell 参数传给普通构建。
- signer audit 只记录 key ID，不打印 PEM 或原始 key。
- 备份使用离线加密介质/硬件设备和双人恢复。
- 怀疑泄露立即执行 [SECURITY.md](../../SECURITY.md) 的 key compromise response。
- 生成、proof、备份、轮换和恢复的可执行步骤见 [官方签名密钥仪式](../security/key-ceremony.md)。

## Current blocker

仓库当前没有官方私钥和正式 signed platform payload，因此只能生成 `.lvapp.dev` 验证产物；任何“正式包”交付都必须等待合法 offline signer provisioning。
