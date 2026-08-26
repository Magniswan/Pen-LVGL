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

## Release evidence bundle

staging 输出目录分为不可签名旁证和待签 artifact。旁证包含 source commit/timestamp、profile identity、官方 public-key hex、manifest 源、`lvapp inspect` 结果、逐文件 size/SHA-256、构建参数、SPDX 2.3 document、license notices 以及 dev package SHA-256/SHA-512。signer 只对 reviewer 批准 SHA-512 的 `.lvapp.dev` 生成 envelope，并另外记录 `key-info.json`、`verification.json` 与最终双摘要。

SBOM 中每个打包文件对应一个 SPDX file 与 checksum，package 通过 `CONTAINS` relationship 关联全部文件；它是审计证据，不替代漏洞扫描或签名验证。
