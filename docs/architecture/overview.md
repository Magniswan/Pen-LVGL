# Architecture Overview

## Trust and process layout

```text
offline official signer
        │ signed platform/app .lvapp
        v
Falcon manager ───────────> platform payload + separate policy state
Falcon launcher (alive)
        │ <hole> + touch overlay
        v
lvgl-sessiond (policy authority)
  ├─ reverify platform/profile
  ├─ canonicalize installed app registry ──FD──> LVGL desktop
  ├─ verify/fexecve selected entry ────────────> LVGL app
  └─ exact-child READY/crash/home/exit supervision
```

Falcon launcher 与 manager 是独立 AMR。launcher 不携带 payload；manager 不负责 hole。LVGL desktop/installer/app 都是 sessiond 的独立 child，崩溃后 sessiond 回到 desktop，而 Falcon miniapp 始终作为 hole 宿主存活。

## Installation planes

Platform roots：

- payload：`/userdisk/apps/lvgl-platform`
- policy：`/userdisk/apps/lvgl-platform-policy`

Application roots：

- payload：`/userdisk/apps/lvgl-apps`
- policy：`/userdisk/apps/lvgl-app-policy`
- inbox：`/userdisk/apps/lvgl-inbox`

policy root 与可移除 payload 分开，避免卸载等价于清空 release/security high-water。release 安装在隔离 staging 中完成，fsync 后提交 state；平台再原子切换 `current`。

## Application launch

1. sessiond 恢复 installed state，复验 current official package 与所有已安装字节。
2. sessiond 生成最大 64 KiB/64 apps 的 canonical registry，通过只读 FD 传给 desktop。
3. desktop 只发送 canonical stable app ID。
4. sessiond 重新验证目标 profile/machine/ABI/capabilities/state/digest/files。
5. sessiond 从已打开、已验证的 entry FD 执行 `fexecve`。
6. child 首次成功 present 后发送 READY；超时/崩溃则回 desktop。

## Input and display

Falcon 页面持有透明 touch layer，最多映射 32 个触点。原生 bridge 发送 nonce、sequence、monotonic timestamp 绑定的 56-byte datagram。sessiond 校验生命周期并转成只读 canonical touch FD。

DRM runtime 只使用 signed profile 中已认证的 connector/CRTC/overlay/rectangle/rotation。`READY` 必须发生在首个成功 KMS present 后，不能以“进程已启动”代替。

## Stable contracts

- platform version：`1.0.0`
- SDK ABI：`1.0`
- LVAPP format：1 / `LVAPP001`
- session control：128-byte little-endian v1
- touch protocol：56-byte little-endian v1
- application identity：小写 canonical reverse-domain ID

不兼容变化必须提升对应 version/ABI/format，而不是静默复用旧值。
