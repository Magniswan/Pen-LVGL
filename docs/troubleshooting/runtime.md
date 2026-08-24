# Runtime Troubleshooting

## Launcher says profile is not certified

这是预期失败关闭。检查 signed platform 中的 `profile.env` 和认证 evidence。仓库 `profiles/y01-4.8.6.json` 仍为 `holeSessionCertified=false`，不要手改为 true 绕过。

## Hole never becomes ready

按顺序检查 root-owned `/run/lvgl-platform/session.status`：

- `STATE/RESULT` 是否提供稳定错误码；
- `SESSION_PID` 的 `/proc/PID/exe` 是否精确指向 current sessiond；
- logical width/height 是否与 launcher profile 一致；
- overlay 首帧是否成功；READY 只在首帧 present 后出现。

不要通过 kill Falcon/miniapp“重置”。退出并重新打开 launcher 页面只会重新 probe/start。

## Desktop has no applications

目标 desktop 缺/坏 `LVGL_APP_REGISTRY_FD` 时故意只显示自身。检查 sessiond 是否能恢复 application policy、验证 current package 和逐文件 metadata/hash。不要增加 target fallback 开发列表。

## Inbox candidate rejected

优先使用 candidate `detail` code：签名/key ID、profile/machine、SDK ABI、capability、counter/epoch/digest、reserved ID、root owner/mode/nlink 或 package layout 任一不符都会拒绝。文件名和扩展名不会放行包。

## App immediately returns to desktop

检查 child READY timeout、entry ELF ABI/NEEDED、首帧 DRM present、未处理 signal/exception，以及 sessiond 传入的 fixed FDs。sessiond 在每次启动前都会复验 installed bytes；磁盘被修改也会直接拒绝。

## Touch missing or stuck

核对 session nonce、sequence、monotonic timestamp、logical bounds 和 contact lifecycle。Falcon onHide 应为所有触点发送 cancel。不要让 app 直接读 evdev；hole 模式只接受 canonical touch FD。

## Build succeeds but device fails

交叉编译只证明语法/链接，不证明 connector/plane/rotation/touch 或动态库版本兼容。必须在匹配 model/firmware/PCBA/libc 的认证设备上完成测试。当前 Nexus 4 不是目标设备，禁止用它试装。
