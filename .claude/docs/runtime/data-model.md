# Runtime Data Models

## Certified Session Profile

闭合 `KEY=VALUE` 文本，字段包括 profile ID、machine、logical size、DRM device、connector/CRTC/overlay plane、zpos、物理矩形、pixel format、rotation 和 `HOLE_SESSION_CERTIFIED`。未知、缺失、重复或未认证值拒绝。

## Touch Frame v1

固定 56 bytes，小端编码。关键约束：magic/version/size 精确；nonce 等于本次 session；sequence 单调；timestamp 不陈旧也不显著来自未来；contact ID ≤31；坐标在逻辑边界；接触生命周期合法。

## Session Control v1

固定 128 bytes。命令：`ready(1)`、`exit_session(2)`、`launch_application(3)`、`home(4)`。launch 的 app ID 上限 95 bytes，其他命令必须空 ID；result 和 reserved 当前必须为零。

## Session Status

原子发布到 `/run/lvgl-platform/session.status`：PID、nonce、state、result、hole/input readiness、logical dimensions。只有 `state=ready` 可同时声明 hole/input ready。

## Registry FD

桌面额外继承 `LVGL_APP_REGISTRY_FD`。目标端若缺少或无法验证该 FD，只显示桌面自身，不使用硬编码应用 fallback。

## Storage Broker v1

`SOCK_SEQPACKET` 上每个 packet 是 96-byte `LVSTOR1` header 加最多 64 KiB data。request 包含 read/write、非零 request ID、canonical record、单记录 maximum；response 回显 command/ID 并返回闭合 status。reserved/padding 必须全零，错误 response 不带 data。sessiond 用 authenticated `maxFiles`/`dataMiB` 评估整个 app 目录，不信任应用自报 quota。

## Program Policy

`ApplicationResourceLimits` 的 memory MiB、CPU seconds、maximum files、data MiB 来自已验签 manifest。sessiond 派生碰撞检测 UID/GID，设置 AS/CPU/core/file-size/open-files/processes/memlock/stack rlimit；runtime marker 存在时，非 AArch64 或 seccomp 安装失败均拒绝运行。
