# Desktop / Installer API

## 固定存储

```cpp
InboxStatus prepare_application_storage() noexcept;
```

只创建并验证以下 root-owned、不可组/其他用户写入的固定目录：

- `/userdisk/apps/lvgl-apps`
- `/userdisk/apps/lvgl-app-policy`
- `/userdisk/apps/lvgl-inbox`

调用方不能提供路径或密钥。这三个底层函数只在 root sessiond broker 内调用；非 root `InstallerUi` 不直接调用它们。

## 扫描

```cpp
InboxScanResult scan_official_inbox(
    const InstallPolicyContext&, const CryptoProvider&);
```

最多读取 128 个目录项；每个包最大约 256 MiB。候选必须是 root-owned、单硬链接、普通文件、不可组/其他用户写入，且打开时使用 `O_NOFOLLOW`。扫描结果包括包身份、版本、高水位、大小、校验详情与是否可安装。

## 安装

```cpp
InboxInstallResult install_official_inbox_candidate(
    const std::string& token,
    const InstallPolicyContext&,
    const CryptoProvider&);
```

`token` 是扫描时包完整字节的 128 位小写十六进制 SHA-512。服务安装时不复用 UI 的旧验证对象，而是重新枚举、读取、验签并执行安装策略，降低选择到安装之间的替换风险。

## Typed broker

`InstallerClient` 从 `LVGL_INSTALLER_FD` 取得 sessiond 创建的 `SOCK_SEQPACKET` endpoint，验证 peer UID 为 root。固定 v2 协议：

- 160-byte request：`scan`、`candidate(index)`、`install(token)`、`installed_scan`、`installed_candidate(index)`、`rollback(token)` 或 `remove(token)`；
- 768-byte response：bounded status/detail/count、一个已判定 `InboxCandidate`，或一个 `InstalledApplicationCandidate`；
- magic `LVINST2`、version 2、非零 request ID、全零 reserved/padding；
- 没有路径、公钥、调用方 policy、包 bytes、manifest 或命令文本。

sessiond 只为 `top.lvgl.installer` 创建该 socket，并以认证 session profile 构造 policy。每个 candidate 查询都会重扫；install 再次按 token 重扫并执行完整官方验签/反回滚/事务链。installed mutation 也会重新枚举并精确匹配 snapshot token。child endpoint 有 30 秒收发 timeout。

## 已安装生命周期

`InstallerClient::scan_installed()` 返回 bounded installed snapshots；`rollback(token)` 与 `remove(token)` 只接受快照 token。rollback 要求 previous 包重新通过官方签名、digest、文件、profile 和 policy 验证，再写双槽 state；remove 先原子隔离 payload 到随机 tombstone，再做同设备、no-follow、有界清理，保留 policy/data。两类操作都写 root-owned 0600 有界审计记录。

## 应用注册表

```cpp
ApplicationRegistryDocument encode_application_registry(...);
ApplicationRegistryDocument decode_application_registry(...);
```

注册表最多 64 个应用、64 KiB，必须是 canonical 编码。sessiond 通过只读 `LVGL_APP_REGISTRY_FD` 传给桌面。桌面仅使用 app ID、名称、版本等展示数据；启动授权仍由 sessiond 执行。

## UI 生命周期

`InstallerUi` 有“收件箱/已安装”两个模式。install、rollback、remove 都只消费 broker 判定；rollback/remove 必须显示动作、应用和保留/隔离语义并要求第二次确认。耗时变更由 LVGL timer 延后到确认画面呈现之后，成功后重新扫描或请求 `home()` 刷新 registry。
