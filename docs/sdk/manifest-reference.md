# Application Manifest Reference

`app.json` 是签名前 source manifest。工具会生成 `signingKeyId` 和按字节排序的 `files`，开发者不能手填这两个字段。

| 字段 | 规则 |
|---|---|
| `formatVersion` | 当前固定 1 |
| `appId` | 小写 reverse-domain，最长 96 UTF-8 bytes |
| `name` | 展示名，最长 64 bytes |
| `version` | 数字 semver，例如 `1.2.0` |
| `releaseCounter` | 每 app 严格单调，最小 1 |
| `securityEpoch` | 安全代际，普通更新不随意提升 |
| `sdkAbi` | 当前固定 `1.0` |
| `minPlatformVersion` | 最低平台数字 semver |
| `entry` | 包内 0755 executable 的安全相对路径 |
| `supportedProfiles` | 严格排序、唯一的 profile ID 列表 |
| `supportedMachines` | 严格排序、唯一，例如 `aarch64` |
| `capabilities` | 严格排序、唯一的能力列表 |
| `limits` | memoryMiB/cpuSeconds/maxFiles/dataMiB |
| `onlinePolicy` | v1 仅 `{ "mode": "offline-v1" }` |

允许 capabilities：

- `audio.output`
- `dictionary.lookup`
- `haptics`
- `microphone`
- `network`
- `scanner`
- `storage.private`

声明 capability 不等于已经获得平台实现或权限。sessiond 会拒绝当前 profile/platform 未支持的能力；应用不得自行绕过。

## Version policy

- 任何正式重发都使用新的 `releaseCounter`，即使 semver 未变。
- security fix 且需要永久拒绝旧安全代际时提升 `securityEpoch`。
- app ID、signer key ID、counter、epoch 和 digest 共同写入设备 policy state。
- 降低 counter/epoch、同 counter 不同 digest、或 signer 不匹配都会失败关闭。

## Paths and files

禁止绝对路径、反斜杠、`.`/`..` 段、空段、控制字符、Windows 保留分隔字符、symlink 和特殊文件。最多 256 files、单路径 240 bytes、manifest 64 KiB、包 256 MiB。
