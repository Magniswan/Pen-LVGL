# SDK Quickstart

## 1. Copy the template

复制 `sdk/template-app`，至少修改：

- CMake target、`APP_ID` 和 `OUTPUT_NAME`
- `app.json` 的 appId/name/version/entry
- `main.cpp` 的 UI 和应用类

app ID 一经公开应保持稳定；不要把名称、路径或版本拼进 ID。

应用源码只包含公开头，通常从聚合入口开始：

```cpp
#include <lvgl_platform/sdk.hpp>
```

不要包含仓库 `src/runtime` 或 `src/shell` 下的内部转发头。

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

若作为独立工程消费 SDK，先安装 SDK component：

```text
cmake --install <platform-build> --component sdk --prefix <sdk-prefix>
```

安装内容为 `include/lvgl_platform/*.hpp` 与 `lib/cmake/lvgl-platform/LvglApplication.cmake`。SDK ABI 固定为 `1.0`，发布前必须进行全量重编和 header compile test。

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
- manifest 声明 `storage.private` 后，可通过 `context.storage` 读写固定名称的有界记录；不要接触存储路径或继承 FD。

私有记录示例：

```cpp
std::array<std::uint8_t, 64> state {};
context.storage.write_atomic("state.v1", state.data(), state.size(), state.size());

std::vector<std::uint8_t> loaded;
context.storage.read("state.v1", loaded, state.size());
```

记录名只允许小写 ASCII、数字、点、下划线和连字符，不能以点开头或包含 `..`。应用请求经过有界 broker 协议，sessiond 强制 manifest 的 `maxFiles`/`dataMiB`，并以临时文件、`fsync` 与 `renameat` 提交；应用仍需自行定义版本、长度、校验和及严格反序列化。不要解析 `LVGL_APP_STORAGE_FD`。

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

- private storage read/write 与 quota 已强制，但 v1 尚无删除、列举或迁移 API；不要自行创建共享可写目录。
- runtime 已强制独立 UID/GID、rlimit 与 AArch64 seccomp；cgroup、只读 mount namespace 和目标固件 sandbox 认证尚未完成。
- 只支持 SDK ABI `1.0`、offline-v1 和一个认证 profile family。
