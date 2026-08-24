# Falcon Manager API

## JS module `lvgl_manager`

```js
await Manager.inspect()
await Manager.install()
await Manager.repair()
await Manager.upgrade()
await Manager.remove()
```

所有方法都拒绝参数。不存在路径、shell、包字节、公钥或 override 输入。

## Build provisioning

生产 native build 必须同时提供：

- `OFFICIAL_PUBLIC_KEY_HEX`
- `MANAGER_CERTIFIED_PROFILE_ID`
- `MANAGER_CERTIFIED_MACHINE`
- `MANAGER_DEVICE_IDENTITY_SHA256_HEX`
- `MANAGER_PLATFORM_PACKAGE`

缺任一项即构建失败。公钥必须是非零 32-byte Ed25519 key，并拒绝仓库测试 key。平台包必须是有界普通文件并作为只读字节嵌入 manager `.so`。

## Operations

| 操作 | 约束 |
|---|---|
| inspect | 验证 build provisioning、嵌入包、设备 identity 和 policy state |
| install | 仅在未安装且无需修复时启用 |
| repair | 已安装或状态退化时重验并恢复 |
| upgrade | 仅 release counter 更高时启用 |
| remove | 要求可信 stopped session；二次 UI 确认 |

安装类操作建立固定平台/application/inbox 目录，执行官方包验签、兼容性和高水位策略，提交双槽 release state，再原子切换 `current` symlink。

remove 先把 payload 根原子改名为随机 tombstone，再以 no-follow、最大深度 32、最多 100000 nodes 的遍历清理。policy state 和 manager AMR 始终保留。
