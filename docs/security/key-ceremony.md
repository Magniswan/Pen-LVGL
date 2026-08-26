# Official Signing Key Ceremony

## Security boundary

生产信任根是一个 32-byte Ed25519 公钥。私钥必须在断网硬件签名设备中生成并保持不可导出；本仓库、构建机、Falcon AMR、词典笔、CI 和发布介质都不得接触私钥。设备只接受编译时固定的这一个公钥，不提供导入、切换或跳过验签入口。

仓库提供的流程只处理公钥、公开 challenge 和 64-byte proof signature。它不会生成、读取或复制生产私钥。proof-of-possession 证明 ceremony 所记录公钥对应的私钥在仪式时可用，但不证明 HSM、人员或物理环境本身可信；这些仍需组织审计。

## Roles and prerequisites

- Custodian A：操作主 HSM/token，不持有另一位 custodian 的认证因子。
- Custodian B：独立核对屏幕/纸面指纹、challenge 摘要和最终证据。
- Reviewer：批准 clean source commit、security epoch 和 activation time，不操作私钥。
- Auditor：在第三台只含公钥的机器上复算 proof 和 checksums。

至少两人全程在场，屏幕录制/纸质记录不得拍到 PIN、recovery secret 或私钥导出界面。生产操作使用固定 Node 18.20.8、可信只读启动介质和校时后的 UTC 时钟。官方 key 不得使用本仓库 RFC 8032 测试向量或全零值。

## 1. Generate and back up the key

1. 在离线 HSM 内生成不可导出的 Ed25519 key；设备标识、固件版本和自检结果写入外部审计记录。
2. 只导出 SubjectPublicKeyInfo PEM。两位 custodian 分别在独立工具上读取 32-byte raw hex 与 key ID，逐字符或分组口头交叉核对。
3. 使用第二个受控 HSM 创建 vendor-wrapped backup，或采用设备支持的 M-of-N 恢复。禁止 plaintext PEM/seed、截图、剪贴板、CI secret 和云盘备份。
4. 主/备 HSM、恢复因子和 PIN 分属不同人员与地点；每次访问写入不可覆盖的纸面或 WORM 审计记录。

## 2. Create the exact challenge

在 clean、reviewed commit 上，用仓库外的公钥文件生成 challenge。witness ID 是审计代号，不写姓名或凭据：

```powershell
./scripts/new_official_key_challenge.ps1 `
  -PublicKey <external-official-public.pem> `
  -ExpectedPublicKeyHex <independently-compared-64-lowercase-hex> `
  -CeremonyId <unique-ceremony-id> `
  -SecurityEpoch <approved-epoch> `
  -ActivationUtc <YYYY-MM-DDTHH:MM:SSZ> `
  -WitnessOne <custodian-a-id> `
  -WitnessTwo <custodian-b-id> `
  -ExpectedSourceCommit <reviewed-40-hex-commit> `
  -OutputFile <external-new-challenge.json> `
  -Node <absolute-node-18.20.8>
```

脚本拒绝 dirty/wrong commit、链接或过大输入、测试/全零/非 Ed25519 key、重复 witness、仓库内输出和覆盖已有文件。Reviewer 通过独立渠道批准脚本打印的 challenge SHA-512；不得从待签 proof 或后续 bundle 反向接受摘要。

## 3. Prove possession and finalize

让 HSM 对 challenge 文件的原始 bytes 签名，包含文件末尾 LF；不要复制粘贴 JSON，也不要重新格式化。输出必须是 raw 64-byte Ed25519 signature，不是 hex/base64 或 ASN.1 wrapper。

随后在 clean exact commit 上完成信任根：

```powershell
./scripts/finalize_official_trust_root.ps1 `
  -Challenge <external-challenge.json> `
  -ProofSignature <external-raw-64-byte-proof.bin> `
  -PublicKey <external-official-public.pem> `
  -ExpectedPublicKeyHex <approved-64-hex> `
  -ExpectedChallengeSha512 <reviewer-approved-128-hex> `
  -ExpectedSourceCommit <reviewed-40-hex-commit> `
  -OutputDirectory <external-new-trust-root-directory> `
  -Node <absolute-node-18.20.8>
```

finalizer 重验 challenge SHA-512、字段、source commit、PEM 摘要、key ID、raw key 和 detached proof，再生成：

- `official-trust-root.json`：唯一可批准的 public trust root、epoch、source 与 witnesses；
- `official-public-key.pem`、challenge 和 proof：供独立复验；
- `proof-verification.json`：proof-of-possession 结果；
- `cmake-arguments.txt`：production build 的唯一 key 参数；
- `checksums.txt`：bundle 内所有证据的 SHA-256。

Auditor 在另一台机器重算 checksums，并再次执行：

```powershell
node tools/lvapp/cli.mjs verify-key-proof `
  --challenge <official-key-challenge.json> `
  --signature <official-key-proof.bin> `
  --public-key <official-public-key.pem>
```

只有 auditor、两位 custodian 和 reviewer 的记录完全一致，才允许把 raw public key 传给 production CMake、manager build、release stager 和 offline signer。最终 bundle 是公开证据，不是授权私钥操作的凭据。

## Signing operations

- Builder 和 reviewer 不接触 HSM；offline signer 只接受批准的 `.lvapp.dev` SHA-512、source commit 和 public-key hex。
- 每次签名需要双人授权，HSM 屏幕必须显示或由可信中间件绑定已批准 artifact digest。
- 生产 signer 不联网、不连接词典笔、不运行 ADB；输出转移后立即在公钥-only 主机复验。
- key usage counter、artifact digest、app ID/version/release counter/security epoch 和最终 package digest 写入 WORM audit log。

## Rotation, loss, and compromise

当前 v1 设备只有一个 trust slot，不能通过普通 app 包自行引入新 key，也没有“同时信任任意两把 key”的隐藏入口。常规轮换或泄露响应必须走经认证的 platform/manager 更新渠道，并在发布前完成真机恢复、降级和断电矩阵；在该外部可信渠道和硬件启动链完成认证前，仓库不宣称支持安全的现场 key rotation。

怀疑 PIN、HSM、签名主机或私钥泄露时：立即停止所有签名/发布，隔离设备和日志，保全证据，撤销未发布产物；建立新 ceremony/新 key，提升受影响应用与平台的 `securityEpoch`，由认证渠道替换 trust root，并永久拒绝旧 key。若无法证明可信启动链和 monotonic state，已 root 控制的设备必须视为不可恢复，不能仅靠重新安装用户态 verifier 恢复信任。

每季度做不触碰生产私钥的流程演练；至少每年在隔离测试 key/HSM 上完成 backup restore、lost-token、invalid proof、wrong digest、old epoch 和 emergency replacement 演练。生产 key 的真实恢复只在明确批准的灾难恢复事件中执行。
