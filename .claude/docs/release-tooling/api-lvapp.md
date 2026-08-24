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
