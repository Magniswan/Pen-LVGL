# Runtime API

## `RuntimeApplication`

```cpp
class RuntimeApplication {
  virtual void create(RuntimeContext&) = 0;
  virtual bool stop_requested() const = 0;
  virtual void destroy();
};
```

`run_platform_application` 依次打开 DRM/Input、初始化 LVGL、调用 `create`、运行 timer/flush/input loop、在首个成功 present 后 `signal_ready`，最后调用 `destroy` 并释放 LVGL/设备。

## `DrmBackend`

- `open()`：解析 session 环境、打开固定 DRM device、验证现有 KMS 路由和 overlay 能力。
- `present_logical(pixels,w,h)`：将 960×266 逻辑帧旋转/映射到 profile 矩形后提交。
- `has_presented_frame()`：READY 的唯一图形依据。
- `close()`：释放自身 buffer/FB/FD，不改变 Falcon 显示模式。

## `InputBackend`

- `open()`：hole 会话要求有效 `LVGL_TOUCH_FD`；普通调试模式才可使用 profile evdev。
- `poll()`：非阻塞消费 canonical touch frame。
- `state()`：返回 LVGL pointer 所需坐标、pressed、changed。

## `AppControl`

| 方法 | 允许方 | 结果 |
|---|---|---|
| `signal_ready()` | 任意前台 child | 首帧后通知 sessiond |
| `launch(stable_id)` | desktop | 请求切换到已注册 app ID |
| `home()` | 非 desktop app | 回到 desktop |
| `exit_session()` | shell | 停止 LVGL 会话，返回 Falcon |

控制协议是 128-byte 固定 little-endian 结构。只有 launch 命令携带 canonical reverse-domain app ID；reserved byte 必须为零。

## sessiond 启动复验

动态应用启动要求：policy state 可恢复、current digest 匹配、官方包签名有效、manifest ID/counter/signer 匹配、当前 profile/machine/ABI/capabilities 兼容、所有安装文件 mode/owner/nlink/size/bytes 一致、entry 是 0755 executable。
