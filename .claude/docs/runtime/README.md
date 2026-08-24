# Device Runtime 基础模块

## 职责

运行时把 Falcon `<hole>` 与一个独立 LVGL 前台进程连接起来。`lvgl-sessiond` 是特权信任边界；应用运行时负责 LVGL、KMS overlay 提交、转发触摸和生命周期回报。

## 调用链

```text
Falcon launcher native plugin
  ├─ fexecve fixed lvgl-sessiond --falcon-hole
  └─ send canonical touch datagrams
lvgl-sessiond
  ├─ verify its own signed release and certified profile
  ├─ bind /run/lvgl-platform/touch.sock
  ├─ derive registry and open verified entry FD
  ├─ capability-gate one root-owned quota storage broker
  ├─ grant one fixed installer broker only to top.lvgl.installer
  └─ fork + UID/GID/rlimit + fexecve one foreground child
foreground app
  ├─ DrmBackend: existing certified overlay plane only
  ├─ InputBackend: inherited touch FD only in hole session
  ├─ mandatory AArch64 seccomp after device initialization
  └─ AppControl: READY / LAUNCH / HOME / EXIT
```

## Display 不变量

- 不 modeset，不改变 CRTC，不接管主 framebuffer。
- connector/CRTC/overlay plane/zpos/rectangle/rotation/pixel format 全由签名 `profile.env` 指定。
- 打开后验证当前 connector→encoder→CRTC 路由、plane type、format 与 zpos。
- 使用 `drmModeSetPlane` 在 hole 对应矩形提交；首帧成功后才能上报 READY。
- 当前 Y01 profile 标记未认证，因此真机启动会安全拒绝；CloudBrowser 数值只是参考证据。

## Input 不变量

- hole 会话禁止 evdev fallback；触摸只来自 sessiond 继承的 datagram socket。
- 56-byte 固定帧包含 session nonce、严格递增 sequence、monotonic timestamp、phase、contact ID 和坐标。
- router 同时校验长度/魔数/版本、nonce、重放、时效、边界与 start/move/end 生命周期。

## Process 不变量

- sessiond 仅接收 `--falcon-hole`，要求 euid 0、`no_new_privs`、不可 dump。
- 只执行已打开且重新验证的 entry FD；不接受路径或 shell。
- 每次切换创建新的 control/touch socketpair；session nonce 与外部触摸序列保持连续。
- 只向精确 fork 的 child PID 发送 SIGTERM/SIGKILL，从不扫描或终止 Falcon/miniapp。
- 动态应用启动失败/崩溃时，仅在 previous release 重新验签、digest/文件/profile/policy 全部通过后提交双槽 rollback；失败 current 被 quarantine 且 high-water 保留，然后回桌面。无可信 previous 时 policy 不变。
- 桌面连续失败三次退出到 Falcon；sessiond 不对 built-in desktop/installer 执行应用 rollback。
- 每个应用映射独立非 root UID/GID；UID 碰撞失败关闭；manifest memory/CPU/files/data limits 映射到 rlimit 与 broker quota。
- `storage.private` 只传 broker socket，不传目录 FD/path/root/key；sessiond 采用 no-follow、0600、bounded read 和原子替换。
- `LVGL_INSTALLER_FD` 只传给 fixed built-in installer；sessiond 保留 inbox/official trust/policy/write transaction，child 只能发送 typed token 请求。
- seccomp 禁止网络 socket、进程派生/exec、mount/ptrace/kill、写路径和 executable mmap；运行期 DRM ioctl 仅 3 项。

## 详细文档

- [api-runtime.md](api-runtime.md)：RuntimeApplication、DRM/Input、会话控制。
- [data-model.md](data-model.md)：profile、touch、control、status。
- [pitfalls.md](pitfalls.md)：设备移植和生命周期陷阱。
- [CHANGELOG.md](CHANGELOG.md)：变更、风险和回滚。
