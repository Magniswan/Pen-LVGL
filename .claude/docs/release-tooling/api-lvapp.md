# LVAPP Tool API

## Source manifest

必须且仅允许：`formatVersion, appId, name, version, releaseCounter, securityEpoch, sdkAbi, minPlatformVersion, entry, supportedProfiles, supportedMachines, capabilities, limits, onlinePolicy`。

- app ID：小写 reverse-domain，最长 96 bytes。
- version：纯数字 semver；SDK ABI 当前固定 `1.0`。
- arrays：UTF-8 byte order 严格升序且无重复。
- capabilities：仅允许工具内白名单。
- `onlinePolicy.mode`：当前只允许 `offline-v1`。
- entry：必须是包内 0755 executable。

## Limits

- 包：256 MiB；manifest：64 KiB；文件：256 个；路径：240 UTF-8 bytes。
- source symlink、特殊文件、绝对/反斜杠/点段/重复分隔符路径均拒绝。
- 普通 asset mode 固定 0644，entry 固定 0755。

## Determinism and signing

`buildDevelopmentPackage` 按 UTF-8 bytes 排序文件，生成 canonical JSON、连续 payload 和零 key ID 的 dev flag 包。输出使用 create-new 语义，不覆盖已有文件。

`signPackage` 只接受 development 包，把 signer public-key SPKI SHA-256 前 16 bytes 写入 key ID，对 `LVAPP-SIGN-V1 || SHA-512(unsigned-package)` 做 Ed25519 签名，并追加 96-byte envelope。

`verifyPackageFile` 拒绝 development 包，要求显式 Ed25519 公钥，同时验证 key ID、布局、canonical manifest、每文件 hash 和签名。

`key-info --public-key` 只接受 Ed25519 public key，输出算法、16-byte key ID 和 32-byte raw public key hex。offline handoff 用 raw hex 与事先批准的官方信任根做字节级相等比较，不能由待签包或私钥反向选择公钥。

`verify-key-proof --challenge --signature --public-key` 要求 raw 64-byte Ed25519 signature，验证 exact challenge bytes，并输出 challenge SHA-512、key ID、raw key 和 `proofValid`。key ceremony 用它证明公开 ceremony record 对应的 HSM key 在仪式时可用。

## Release script APIs

- `stage_platform_release.ps1`：输入 production build、认证 profile、输出目录、官方公钥 hex、version/counter/epoch；验证 build cache 绑定当前 source，执行 clean rebuild 后输出确定性 `.lvapp.dev` 与审计旁证，不接受私钥。
- `sign_release.ps1`：输入 `.lvapp.dev`、仓库外 private key、public key、批准的 raw public key hex、dev SHA-512 与 40-hex source commit；要求 clean exact commit，输出正式 `.lvapp`、verification/key-info/source-evidence 和双摘要。
- `new_official_key_challenge.ps1` / `finalize_official_trust_root.ps1`：只处理 public key、公开 challenge 和 detached proof；固定双 witness、epoch、activation、source 与 Node，输出仓库外 public trust-root evidence bundle，绝不接受私钥参数。
- `build_launcher.ps1` / `build_manager.ps1`：输入精确 Node executable；production manager 额外要求全部 provisioning。输出 AMR path 和 SHA-256，不调用 ADB。

四个入口和 `tools/lvapp` 都要求 Node 18.20.8；不同 Node 版本失败关闭。
