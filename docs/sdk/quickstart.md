# SDK Quickstart

## 1. Copy the template

复制 `sdk/template-app`，至少修改：

- CMake target、`APP_ID` 和 `OUTPUT_NAME`
- `app.json` 的 appId/name/version/entry
- `main.cpp` 的 UI 和应用类

app ID 一经公开应保持稳定；不要把名称、路径或版本拼进 ID。

## 2. Register the target

在顶层 CMake 的 target runtime 区域添加子目录，或直接调用：

```cmake
lvgl_add_application(my_app
    APP_ID top.vendor.myapp
    OUTPUT_NAME lvgl-myapp
    SOURCES main.cpp model.cpp ui.cpp
    INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}"
)
```

helper 自动链接 `platform_runtime` 和 `app_shell`、注入 `LVGL_APPLICATION_ID`，并开启 warning-as-error。应用不要自行打开 DRM、evdev、session socket 或 `/userdisk` 路径。

## 3. Implement lifecycle

```cpp
class MyApplication final : public dictpen::RuntimeApplication {
public:
    void create(dictpen::RuntimeContext& context) override;
    bool stop_requested() const override;
    void destroy() override;
};
```

- `create` 在 LVGL/DRM/input 已初始化后运行。
- 首个成功 KMS present 后 runtime 自动发送 READY。
- `stop_requested` 应合并 `AppShell::stop_requested()` 和 app 自身退出条件。
- `destroy` 删除 timer/event/object 持有者，必须可在失败路径调用。
- 回桌面使用 `context.control.home()`；不要终止进程或 Falcon。

## 4. Build

```text
cmake -S . -B build/platform-aarch64 -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-aarch64.cmake
cmake --build build/platform-aarch64 --target my_app
```

用 `file`/`readelf` 确认 AArch64、interpreter、NEEDED 和无 RPATH/RUNPATH。目标二进制只能依赖认证固件存在的动态库。

## 5. Build a development package

```powershell
./sdk/package-app.ps1 `
  -Manifest path/to/app.json `
  -Executable build/platform-aarch64/lvgl-myapp `
  -Output myapp.lvapp.dev
```

dev 包可用于格式、可复现性和签名前检查，但设备安装器会拒绝。正式签名见 [release process](../release/release-process.md)。

## 6. Tests

- 把纯模型放在不依赖 LVGL 的 `.cpp/.h` 中并做 host tests。
- 对 IPC/manifest/path/security invariants 添加 contract/source tests。
- 动画期间最多排队一个 gesture，避免状态与视觉脱节。
- 在认证硬件上验证首帧、旋转、触控边界、home、崩溃返回 desktop 和内存上限。

## Current SDK limitations

- private storage API/quota 尚未落地；不要自行创建共享可写目录。
- runtime sandbox（独立 UID/seccomp/namespace/cgroup）尚未强制。
- 只支持 SDK ABI `1.0`、offline-v1 和一个认证 profile family。
