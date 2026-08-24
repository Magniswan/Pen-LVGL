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

## Registry document

每条 `RegisteredApplication` 保存 app ID、名称、版本、release counter、security epoch 与 capability count。编码必须唯一；解码拒绝无效布局、非法值和非 canonical 表示。

## Installed application

payload 与 anti-rollback policy 分根保存。active release 由双槽 state 指定；sessiond 启动时校验：

1. state 可恢复且 app ID 匹配；
2. 当前 release digest、签名、key ID/counter/epoch 匹配；
3. profile、machine、ABI、capability 兼容；
4. 每个已安装文件的 owner、mode、nlink、大小和内容匹配；
5. entry 是可执行普通文件，再从已验证 FD 启动。

## Built-ins

- `top.lvgl.desktop`：会话桌面。
- `top.lvgl.installer`：平台内置安装器，不允许应用包替换。
- `top.lvgl.game2048`：开发期内置；存在有效正式安装版本时可由 registry 提供动态版本。
