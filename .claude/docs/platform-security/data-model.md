# Security Data Models

## `.lvapp` v1

```text
64-byte fixed header
canonical UTF-8 JSON manifest (<= 64 KiB)
sorted bounded regular-file table
concatenated payload bytes
96-byte signature envelope (production only)
```

签名为 Ed25519(domain-separated SHA-512(all unsigned bytes))。每个文件另有 SHA-256，用于包检查和安装树逐字节复测。

关键 manifest 字段：`appId`、`name`、`version`、`releaseCounter`、`securityEpoch`、`sdkAbi`、`minPlatformVersion`、`entry`、profiles、machines、capabilities、limits、onlinePolicy、signingKeyId、files。

## `ApplicationReleaseState`

| 字段 | 不变量 |
|---|---|
| `generation` | 每次有效状态迁移递增 |
| `current_release` / digest | 唯一可启动版本 |
| `previous_release` / digest | 一次受控回滚候选 |
| `high_release` / digest | 不因回滚或卸载降低 |
| `quarantined_release` | 首启失败版本，不可按相同字节重激活 |
| `high_security_epoch` | 安全纪元高水位 |
| `signing_key_id` | 防止 app ID 被另一签名者接管 |

状态编码上限 512 bytes，尾部 SHA-256 检测损坏；`state.a`/`state.b` 同 generation 不同内容视为 split-brain。

## Desktop Registry v1

`LVREG01` 固定头 + 最多 64 条长度前缀记录，总计不超过 64 KiB。记录仅含已验证 app ID、显示名、版本、release counter、security epoch 和 capability count；严格按 app ID 排序且唯一。

registry 不是授权数据库。sessiond 每次从签名包和 current state 派生它，通过只读继承 FD 交给桌面；启动时再次复验应用。

## Inbox Token

token 是整个候选文件的 64-byte SHA-512 小写十六进制（128 字符），用于将一次 UI 选择绑定到重新扫描后的同一字节内容。它不是签名或秘密，也不能替代官方验签。

## Installed Snapshot Token

已安装 token 也是 128 字符小写 SHA-512，但使用独立 domain `LVGL-INSTALLED-SNAPSHOT-v1`，覆盖 canonical app ID、payload directory 的 device/inode/ctime，以及 release state 的 generation/current/previous/high/quarantine/security epoch/failure count/key ID/三个 digest；无可信 state 时显式写入 `untrusted-policy`。它将 rollback/remove 绑定到枚举时的完整快照，不能跨状态复用。

## Lifecycle audit / tombstone

每个 app 的 audit 是 root-owned 0600 bounded append log，记录 epoch seconds、`rollback|remove`、`BEGIN|COMMIT|ISOLATED` 与 generation。payload remove 的 tombstone 名为 `.removed-<24 lowercase hex>`，位于 payload apps root；清理只在同一 device 内递归，最大深度 128、节点 8192，中断后可由下次请求继续。
