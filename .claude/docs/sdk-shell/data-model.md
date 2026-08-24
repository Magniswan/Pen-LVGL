# SDK Data Model

## Source Manifest

应用开发者维护不含 `signingKeyId` 和 `files` 的 `app.json`；构建器从真实 regular files 推导排序文件表、mode、role、size 和 SHA-256。关键约束：

- `appId` 一经发布不可变；小写反域名格式。
- `version` 是用户可见 semver；`releaseCounter` 是严格递增发布序号。
- `securityEpoch` 仅在需要拒绝旧安全世代时递增，不能降低。
- `entry` 指向包内唯一 0755 executable。
- `supportedProfiles`/`supportedMachines` 必须排序唯一且反映实际认证。
- v1 capability 闭合 allowlist 当前应用仅使用 `storage.private`。
- `onlinePolicy.mode` 固定 `offline-v1`；在线授权/吊销是后续 TODO。

## RuntimeContext

含 `RuntimeMetrics&`、`AppControl&` 与 `AppStorage&`，引用生命周期覆盖 `RuntimeApplication` 的 create/run/destroy。不得缓存到进程退出之后或跨线程无同步访问。只有 manifest 声明 `storage.private`（或内置 2048）时 storage 才可用。

## AppDescriptor

桌面内部 descriptor 包含 numeric legacy ID、stable ID、display name、executable（动态项为空）、symbol 和 summary/version。授权来源是 sessiond registry FD，不是该结构本身。
