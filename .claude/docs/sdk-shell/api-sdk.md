# SDK API

## Runtime

| 类型/方法 | 契约 |
|---|---|
| `RuntimeContext.metrics` | 只读采样/诊断对象，不作为游戏计时真相 |
| `RuntimeContext.control` | 当前 session 的类型化导航控制 |
| `RuntimeContext.storage` | capability-gated app-private 固定记录存储；不可用时 fail closed |
| `RuntimeApplication::create` | 仅 runtime 初始化后调用一次 |
| `stop_requested` | 快速、无副作用；每轮主循环调用 |
| `destroy` | 幂等清理 timers/animations/业务资源 |
| `run_platform_application` | 进程唯一 runtime 入口 |

## Navigation

- 普通应用只调用 `AppControl::home()` / `exit_session()`。
- 只有 desktop 调用 `launch(stable_id)`。
- `available()` 为 false 时 UI 应恢复可交互并显示连接失败，不能直接退出或 exec。

## Theme

颜色 token：`ink`、`muted`、`canvas`、`surface`、`line`、`accent`、`accent_pressed`、`danger`。布局常量：960×266、gesture 18、panel 82、touch target 44。

`style_screen` 清除滚动和默认边框；`style_button` 统一 radius/pressed；`create_label`/`create_button` 绑定应用字体。

## Metrics

`RuntimeMetrics::snapshot()` 提供 FPS、平均/峰值 frame time、CPU、RSS、frames、input count 和最后 pointer。只在诊断/测试启用周期日志，避免常驻 I/O。

## Storage

- `available()`：sessiond 是否授予当前 app 私有 storage broker。
- `read(record, output, maximum_size)`：通过 canonical `SOCK_SEQPACKET` 请求读取 root-owned 0600、单硬链接 regular record；拒绝 symlink、空文件、超限和尾随增长。
- `write_atomic(record, data, size, maximum_size)`：broker 强制 `maxFiles`/`dataMiB` 后执行 0600 随机临时文件 → file fsync → 同目录 rename → directory fsync。
- record 不是路径；只允许闭合字符集、最长 64 bytes、非隐藏名且不含 `..`。
- 调用是同步的，单次最多 64 KiB，超时、协议错误、quota 或损坏统一失败；应用不得读取/传递继承 socket FD。

## Packaging

开发包：`node tools/lvapp/cli.mjs build --manifest app.json --root root --out app.lvapp.dev`。

离线发布：`sign --input ...dev --key <offline PEM> --out app.lvapp`。开发机可 `inspect`；发布验证用 `verify --public-key`。设备端不接受命令行公钥，而使用编译官方根。
