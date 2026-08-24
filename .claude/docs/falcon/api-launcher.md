# Falcon Launcher API

## JS module `lvgl_launcher`

```js
await Launcher.probe()
await Launcher.start()
await Launcher.status()
Launcher.sendTouch({ phase, contactId, x, y })
```

前三个方法不接受参数。`start` 只打开固定路径 `/userdisk/apps/lvgl-platform/current/bin/lvgl-sessiond`，验证 owner/mode 后使用 `fexecve` 启动，并只传 `--falcon-hole` 与固定 PATH。

## 状态结果

| 字段 | 含义 |
|---|---|
| `available` | release/profile 通过 launcher 预检 |
| `accepted` | 本次 start 已 fork 新 sessiond |
| `holeReady` | session status 为 ready 且 hole flag 有效 |
| `inputReady` | hole、尺寸和输入 flag 一致 |
| `state` | idle / launching / starting / ready / error |
| `sessionPid` | 经 `/proc/<pid>/exe` 验证的 sessiond PID |
| `logicalWidth/Height` | certified profile 的 hole 逻辑尺寸 |
| `result/message` | 平台结果码与人类可读诊断 |

## `<hole>` 页面

页面在 `holeReady` 后渲染持续存在的 `<hole>`，其上叠加透明 touch layer。onHide/onUnload 只取消触点、停止轮询并隐藏 hole；不得调用 stop、kill 或退出 miniapp 的接口。

## Touch API

- `phase`：`start | move | end | cancel`
- `contactId`：0–31，由 Falcon touch identifier 稳定映射
- `x/y`：先由页面限制到 certified logical bounds，原生层再次验证

原生层发送固定 56-byte little-endian 帧：magic、version、size、session nonce、严格递增 sequence、monotonic timestamp、phase、contact ID、x、y、reserved。sessiond 独立验证后才产生 canonical input FD。
