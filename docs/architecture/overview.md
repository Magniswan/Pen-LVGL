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
  ├─ fixed installer broker ───────────socket──> built-in installer only
  ├─ root-owned quota storage broker ──socket──> LVGL app
  ├─ open exact DRM device ───────────────FD───> LVGL app
  ├─ verify/fexecve + drop UID/GID + rlimit ───> LVGL app
  │                                               └─ mandatory AArch64 seccomp
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

安装器 child 与普通应用一样是非 root 且受 seccomp 限制，不能直接打开上述 root-owned roots。只有固定 `top.lvgl.installer` 获得 160/768-byte `SOCK_SEQPACKET` endpoint；sessiond 端接受 inbox scan/candidate/install 与 installed scan/candidate/rollback/remove。所有变更命令只携带 128 个小写十六进制字符的 SHA-512 快照 token；broker 重新枚举、复验并匹配 token 后才操作自身解析出的 canonical app ID，协议不含路径、公钥、manifest 或 shell 字段。

手动 rollback 必须完整复验 previous 的官方签名、包摘要、逐文件内容和当前 profile/policy，随后写双槽 state；原 current 进入 quarantine，high-water 不下降，且手动操作不会伪造 launch-failure 计数。remove 先把唯一 app payload 目录原子重命名为随机 tombstone，再执行 no-follow、同设备、最大 128 层/8192 节点的有界清理（覆盖格式允许的最深 240-byte canonical path）；中断后由下次生命周期请求继续清理。policy 与 `lvgl-data` 从不进入删除根。每次破坏性操作在独立 0700 audit 目录写入有界 0600 BEGIN/COMMIT（或 ISOLATED）记录。

## Application launch

1. sessiond 恢复 installed state，复验 current official package 与所有已安装字节。
2. sessiond 生成最大 64 KiB/64 apps 的 canonical registry，通过只读 FD 传给 desktop。
3. desktop 只发送 canonical stable app ID。
4. sessiond 重新验证目标 profile/machine/ABI/capabilities/state/digest/files。
5. sessiond 拒绝 UID 映射碰撞，打开精确 DRM FD，应用 `setgroups(0)`、独立非 root UID/GID 与 manifest rlimit 后，从已验证 entry FD 执行 `fexecve`。
6. runtime 完成 DRM/input 初始化后安装强制 AArch64 seccomp：禁止进程/网络/挂载/写路径/可执行映射，DRM ioctl 仅允许 `SETPLANE`、`RMFB`、`DESTROY_DUMB`。
7. child 首次成功 present 后发送 READY；动态应用启动失败/崩溃时，sessiond 先完整复验 previous release，再原子提交 rollback，记录失败 current 为 quarantined 并保留 high-water；没有可信 previous 时只回 desktop，不改 policy。

`top.lvgl.game2048` 可由有效正式安装版本覆盖；该动态 release 缺失/无效时只允许回退到官方签名平台包内的固定 built-in executable。任意其他 app ID 没有路径或可执行兜底。

## Private storage

`storage.private` 不再把目录 FD 交给应用。sessiond 保持 root-owned 0700 目录和 0600 records，只给应用一个 `SOCK_SEQPACKET` broker endpoint。固定 96-byte v1 header 绑定 command/request ID/record/size；包最大 65,632 bytes，单记录最大 64 KiB。每次读写都重新扫描 owner/type/mode/nlink/size，按已签名 manifest 的 `maxFiles` 与 `dataMiB` 强制 quota，写入使用随机临时文件、file fsync、rename 和 directory fsync。

## Input and display

Falcon 页面持有透明 touch layer，最多映射 32 个触点。原生 bridge 发送 nonce、sequence、monotonic timestamp 绑定的 56-byte datagram。sessiond 校验生命周期并转成只读 canonical touch FD。

DRM runtime 只使用 signed profile 中已认证的 connector/CRTC/overlay/rectangle/rotation。`READY` 必须发生在首个成功 KMS present 后，不能以“进程已启动”代替。

## Stable contracts

- platform version：`1.0.0`
- SDK ABI：`1.0`
- LVAPP format：1 / `LVAPP001`
- session control：128-byte little-endian v1
- touch protocol：56-byte little-endian v1
- storage broker：96-byte header / `LVSTOR1` / v1
- installer broker：160-byte request / 768-byte response / `LVINST2` / v2
- application identity：小写 canonical reverse-domain ID

不兼容变化必须提升对应 version/ABI/format，而不是静默复用旧值。
