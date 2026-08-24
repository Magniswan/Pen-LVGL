# Configuration and Build Environments

## 环境区分

| 环境 | 用途 | 信任状态 |
|---|---|---|
| host-test | Windows/MSVC 契约与模型测试 | 可使用测试 vectors；不产生产物 |
| target-dev | WSL + AArch64 toolchain 编译/开发 AMR | 官方 key/payload 默认空，生产安装失败关闭 |
| target-production | 认证 profile 的平台和两个 AMR | 必须注入官方公钥、签名包与设备 identity |

环境不是运行时 UI 选项。production 与 development 由构建参数和签名产物分离，设备端没有切换按钮。

## 文件命名

- `profiles/<model-or-profile>.json`：研究/认证证据，符合 `profiles/schema.json`。
- 平台包内 `profile.env`：sessiond 实际消费的签名 canonical 配置。
- `cmake/toolchain-aarch64.cmake`：目标交叉编译工具链。
- `platform/.../official_key_config.h.in`：CMake 生成官方公钥头；不得手改生成文件。
- `manager_build_config.h` / manager official key header：开发占位；生产由 build script 临时生成并覆盖 include 优先级。

## 平台 CMake 配置

| 项 | 默认 | 说明 |
|---|---|---|
| `LVGL_PLATFORM_PRODUCTION_BUILD` | `OFF` | `ON` 时无官方公钥即配置失败 |
| `LVGL_PLATFORM_OFFICIAL_PUBLIC_KEY_HEX` | 空 | 32-byte Ed25519 公钥的 64 位小写 hex；拒绝零 key 和 RFC 测试 key |
| `DICTPEN_TOOLCHAIN_ROOT` | ARM GNU 11.3 路径 | 可作为 CMake cache path 覆盖 |
| `DEVICE_LIBDRM_PATH` | repo sysroot 路径 | 不存在时不构建 DRM runtime/apps |

示例仅展示公钥占位，不应把真实 key 写进 shell history 或仓库：

```text
cmake -S . -B build-target \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-aarch64.cmake \
  -DLVGL_PLATFORM_PRODUCTION_BUILD=ON \
  -DLVGL_PLATFORM_OFFICIAL_PUBLIC_KEY_HEX=<official-public-key>
```

私钥绝不能传给 CMake、manager build 或设备。

## Manager 生产输入

`scripts/build_manager.ps1 -Production` 要求：

- `OfficialPublicKeyHex`
- `CertifiedProfileId`
- `CertifiedMachine`
- `DeviceIdentitySha256Hex`
- `PlatformPackage`

脚本转换为临时 WSL 环境变量供 native build 消费。缺项、格式错误、测试 key 或非普通 payload 均失败。开发构建可留空，但所得 manager 会返回 `MANAGER_BUILD_UNPROVISIONED`。

## Launcher native 编译

`launcher/native/CMakeLists.txt` 要求成对设置 `CROSS_C_COMPILER` + `CROSS_CXX_COMPILER`，或设置 `CROSS_TOOLCHAIN_PREFIX`。`tools/build-native.sh` 默认使用 ARM GNU 11.3。Falcon 使用 QuickJS `20200705` 与 `falcon-ui 1.0.3`。

## Device profile

当前 `youdao-y01-4.8.6` 为 legacy POC evidence，`holeSessionCertified=false`。它不能作为商用认证。生产 `profile.env` 必须处于官方签名平台包中，至少覆盖 profile/machine、logical size、DRM connector/CRTC/overlay、display rectangle/rotation、pixel format 与 `HOLE_SESSION_CERTIFIED=1`。

sessiond 不接受未知字段、重复字段、越界矩形或未认证 hole。运行时 `LVGL_*` 环境由 sessiond 构造；普通 app 不应读取调用者环境来改变信任、路径或设备选择。

## 设备操作边界

设备脚本要求明确且唯一的 ADB 目标，但“唯一设备”不等于“正确设备”。当前附加 Nexus 4 不在目标范围，禁止执行任何安装、卸载或写入。商用脚本后续应增加 serial allowlist + 设备 identity 双重确认。
