# Desktop / Installer API

## 固定存储

```cpp
InboxStatus prepare_application_storage() noexcept;
```

只创建并验证以下 root-owned、不可组/其他用户写入的固定目录：

- `/userdisk/apps/lvgl-apps`
- `/userdisk/apps/lvgl-app-policy`
- `/userdisk/apps/lvgl-inbox`

调用方不能提供路径或密钥。

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

## 应用注册表

```cpp
ApplicationRegistryDocument encode_application_registry(...);
ApplicationRegistryDocument decode_application_registry(...);
```

注册表最多 64 个应用、64 KiB，必须是 canonical 编码。sessiond 通过只读 `LVGL_APP_REGISTRY_FD` 传给桌面。桌面仅使用 app ID、名称、版本等展示数据；启动授权仍由 sessiond 执行。

## UI 生命周期

`InstallerUi` 执行 inspect → verify → install → result。耗时安装由 LVGL timer 延后到确认画面呈现之后。成功后请求 `home()`，促使 sessiond 回桌面并重新生成 registry。
