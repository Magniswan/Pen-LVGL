# SDK / Shell 基础模块

## 当前公开面

后续应用以 `RuntimeApplication` 作为生命周期入口，使用 LVGL 创建界面，以 `AppShell` 获得统一返回/退出面板，以 `AppControl` 请求 HOME。应用不直接操作 session socket、Falcon 或可执行路径。

```cpp
class MyApplication final : public dictpen::RuntimeApplication {
  void create(dictpen::RuntimeContext& context) override;
  bool stop_requested() const override;
  void destroy() override;
};

int main() {
  MyApplication app;
  dictpen::PlatformRuntimeOptions options;
  options.app_id = "com.example.app";
  return dictpen::run_platform_application(app, options);
}
```

## 生命周期

1. 构造应用对象，不访问 LVGL。
2. runtime 完成设备和 LVGL 初始化后调用 `create`。
3. `create` 建立业务 UI，再建立 `AppShell`，使 shell 位于 top layer。
4. 主循环查询 `stop_requested`；不要自行阻塞或创建第二个 LVGL 线程。
5. `destroy` 删除 timers/animations/非 LVGL 资源；随后 runtime `lv_deinit`。

## UI 约定

- 逻辑画布 960×266，横向信息密度高；最小触摸目标 44 px。
- 顶部 18 px 保留给下拉 shell 手势，不把唯一关键操作放在该区域。
- 默认 precision-instrument 色板由 `dictpen::theme` 提供。
- 中文使用仓库许可记录的应用字体；Montserrat 用于数字和符号。
- 尊重 reduced-motion；长动画必须可跳过且模型状态始终是权威。

## Shell

`AppShellConfig{title,is_launcher}`：普通应用的面板提供 HOME 与退出 Falcon 会话；桌面只提供退出。应用同时检查自身 UI 与 shell 的 `stop_requested()`。

## 稳定性状态

- 当前 SDK ABI 标识为 `1.0`，manifest 必须精确匹配。
- `RuntimeApplication`、`RuntimeContext`、`AppControl`、`AppStorage` 和基础 theme 是现有最小接口。
- `AppStorage` 已通过 root-owned broker 提供有界原子 record API，并强制 signed manifest quota。删除/列举、其他 capability broker、haptics、安全日志、cgroup/namespace 和真机认证仍未完成；应用不得自行发明全局存储路径。

## 详细文档

- [api-sdk.md](api-sdk.md)：类与方法契约。
- [data-model.md](data-model.md)：app manifest 与 runtime context。
- [pitfalls.md](pitfalls.md)：LVGL 生命周期和分发误区。
- [CHANGELOG.md](CHANGELOG.md)：SDK 变更与兼容风险。
