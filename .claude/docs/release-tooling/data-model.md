# LVAPP Package Data Model

## Unsigned region

```text
64-byte header
canonical UTF-8 manifest JSON
sorted variable-length file table
contiguous payload bytes
```

header 以 `LVAPP001` 开始，固定 format/header version、flags、manifest/entry/table/payload/unsigned/total sizes 和 hash algorithm ID。所有整数是 little-endian；reserved 必须为零。

每个 file-table record 包含 path length、mode、flags、size、payload offset、SHA-256 和 UTF-8 path。路径严格递增；offset 连续；table、manifest files 和 payload 必须一一一致，不能存在未记账 bytes。

## Signature envelope

正式包在 unsigned region 后追加固定 96 bytes：`LVSIG001`、envelope/algorithm version、16-byte key ID、64-byte Ed25519 signature、zero reserved。

签名消息为：

```text
ASCII/zero-padded 16-byte domain "LVAPP-SIGN-V1"
|| SHA-512(unsigned region)
```

## Development vs production

development 包设置 dev flag、无 envelope、manifest key ID 全零。production 包清除 dev flag，total size 包含 envelope，key ID 必须非零并与 signer public key 匹配。

## Manifest policy fields

`releaseCounter` 为每 app 单调版本轴，`securityEpoch` 用于安全代际；`sdkAbi/minPlatformVersion/supportedProfiles/supportedMachines/capabilities/limits/onlinePolicy` 共同构成设备安装与启动策略输入。
