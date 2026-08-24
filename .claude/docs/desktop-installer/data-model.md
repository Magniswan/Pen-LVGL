# Desktop / Installer Data Model

## `InboxCandidate`

| 字段 | 含义 |
|---|---|
| `token` | 包完整字节 SHA-512，小写十六进制 |
| `app_id` / `name` / `version` | 已验签 manifest 的展示身份 |
| `release_counter` | 单调发布计数器 |
| `security_epoch` | 安全代际 |
| `package_size` | 被验证的包长度 |
| `installable` | 当前设备策略是否允许安装 |
| `detail` | 稳定的审计/错误代码 |

同 digest 的候选会去重并按 token 排序，避免目录顺序影响 UI。

## Installer broker wire model

request 固定 160 bytes，response 固定 768 bytes；两者均使用 `LVINST1`/v1、little-endian、非零 request ID 和全零 reserved/padding。response 的 candidate 区域固定限制 token 128、app ID 96、name 96、version 32、detail 256 bytes。非 candidate response 必须把 candidate 字段全部清零，阻止字段走私和歧义解释。

## Registry document

每条 `RegisteredApplication` 保存 app ID、名称、版本、release counter、security epoch 与 capability count。编码必须唯一；解码拒绝无效布局、非法值和非 canonical 表示。

## Installed application

payload 与 anti-rollback policy 分根保存。active release 由双槽 state 指定；sessiond 启动时校验：

1. state 可恢复且 app ID 匹配；
2. 当前 release digest、签名、key ID/counter/epoch 匹配；
3. profile、machine、ABI、capability 兼容；
4. 每个已安装文件的 owner、mode、nlink、大小和内容匹配；
5. entry 是可执行普通文件，再从已验证 FD 启动。

失败 rollback 不直接相信 state 中的 `previous_release`：sessiond 重新打开对应 release 目录、官方验签 `.package.lvapp`、匹配 previous digest/key/counter、逐文件复验并执行当前 profile policy。成功后新 generation 把原 current 写入 `quarantined_release`、previous 提升为 current、清空 previous，但保留 `high_release/high_digest/high_security_epoch`。

## Built-ins

- `top.lvgl.desktop`：会话桌面。
- `top.lvgl.installer`：平台内置安装器，不允许应用包替换。
- `top.lvgl.game2048`：开发期内置；存在有效正式安装版本时可由 registry 提供动态版本。
