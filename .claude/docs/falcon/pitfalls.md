# Falcon Pitfalls

## 不要杀 miniapp

launcher 是 hole 的宿主。隐藏页面时只能停止轮询并取消触点；杀 Falcon/miniapp 会破坏 hole 生命周期，也违反本项目的宿主隔离要求。会话结束由 sessiond 自身协议控制。

## 不要让 AMR 管理 payload 路径

host install/uninstall 脚本只操作各自 AMR。平台 payload 由 manager 内嵌包事务安装，launcher AMR 的卸载不能触碰 `/userdisk/apps/lvgl-platform`。

## 认证 profile 默认失败关闭

仓库 profile 尚未被硬件认证，`HOLE_SESSION_CERTIFIED=0`。在真实设备测得 overlay/rotation/input 约束并生成生产 identity 前，launcher 必须拒绝启动；不要为演示改成 1。

## JS 检查不是安全边界

按钮 enable、二次确认和字段 normalization 改善 UX，但真正授权必须留在 native manager/sessiond。Falcon 页面可能被 root patch。

## 启动不是监督

Falcon launcher 只负责一次受限 start 与状态/触控桥。sessiond 独立监督 desktop 和 apps；不要把 child 生命周期重新绑到页面 onHide/onUnload。

## Root 对手

manager 的 identity fingerprint 只提高误装和用户态篡改门槛，不是硬件 attestation。root/内核对手仍能 patch launcher、manager 或 sessiond；生产商用需要 verified boot、dm-verity/IMA、密钥轮换和硬件根信任配合。

## 不要把开发与生产 AMR 当成同一布局

生产 `-c -q -p` 输出 `app.js.bin`，开发 `-p` 输出 `app.js`。校验器必须由构建模式选择预期脚本名，并要求归档恰好只有脚本、`app_icon.png`、对应 AArch64 native `.so` 和 `manifest.json`；不得用“至少包含”或通配逻辑放行附加内容。

Falcon CLI 会写入当前 ZIP 时间，导致相同内容的 AMR 外层摘要漂移。共同构建器必须先验原包，再以固定 UTF-8 名称顺序、compression 和 1980 UTC 时间重封装，最后再次执行完整归档校验；不能只改时间后直接发布。

## 不要依赖全局 Falcon 工具

发布只接受 Node 18.20.8 和项目声明/安装均为 `aiot-vue-cli 1.0.32`。全局 CLI、自动升级或未锁版本会改变 QuickJS 输出与 AMR bytes，不能进入可审计发布链。

Windows→WSL 路径必须直接交给 `wslpath`，不能依赖 `bash -lc` 的位置参数；后者在部分 `wsl.exe` 版本会静默丢失 `$1`，并把当前目录误当目标路径。
