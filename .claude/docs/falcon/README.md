# Falcon AMR Applications

## 概述

项目包含两个职责分离的 Falcon 应用：

- `manager`（appid `8080992608050002`）：安装、修复、升级、移除官方 LVGL 平台。
- `launcher`（appid `8080992608050001`）：保留 Falcon miniapp，通过 `<hole>` 显示 LVGL，并转发认证触控帧。

manager 拥有内嵌的官方平台包；launcher 不携带、不安装平台 payload。launcher 只启动固定的 `lvgl-sessiond --falcon-hole`，没有终止 Falcon、miniapp 或会话的路径。

## 生命周期

```text
Falcon launcher page
  -> probe certified installed release
  -> start fixed sessiond through verified FD
  -> poll root-owned session.status
  -> HOLE_READY=1 时显示持续存在的 <hole>
  -> touch overlay -> nonce/sequence framed datagrams
  -> onHide: cancel contacts and stop polling only
```

manager 的 install/repair/upgrade 都复用同一官方验签与事务安装路径；remove 只移除 platform payload，保留 manager AMR 和 anti-rollback policy。

## 依赖

- [Platform Security](../platform-security/README.md)
- [Device Runtime](../runtime/README.md)
- Falcon QuickJS / AMR toolchain

## 详细文档

- [api-launcher.md](api-launcher.md)：hole、状态与触控桥 API。
- [api-manager.md](api-manager.md)：固定平台管理 API。
- [data-model.md](data-model.md)：状态快照与触控帧。
- [pitfalls.md](pitfalls.md)：miniapp 生命周期、生产配置与 root 边界。
- [CHANGELOG.md](CHANGELOG.md)：变更与验证。
