# Youdao Dictionary Pen LVGL Platform

面向有道词典笔的离线 LVGL 应用平台：两个 Falcon AMR、持久 `<hole>` 会话、原生桌面/安装器、仅官方签名的 `.lvapp` 分发链、应用 SDK，以及动画 2048 参考应用。

> 当前状态：工程化预生产基线，不是已认证商品固件。仓库没有官方私钥和正式 payload；Y01 profile 仍标记为未通过 hole 真机认证，因此生产路径会失败关闭。

## 组成

| 组件 | 职责 |
|---|---|
| Falcon manager | 安装、修复、升级、移除官方签名平台；保留 anti-rollback state |
| Falcon launcher | 宿主持久 `<hole>`、启动固定 sessiond、转发认证触控；从不杀 miniapp |
| `lvgl-sessiond` | 复验平台与应用、生成桌面 registry、持有 storage/installer brokers、独立监督一个前台 child |
| LVGL desktop | 展示 sessiond 提供的 app ID；不持有路径或启动授权 |
| LVGL installer | 非 root UI 只使用 typed broker；安装仅接受官方包，并以二次确认执行 token-bound 回滚/卸载 |
| App SDK | `RuntimeApplication`、`AppShell`、CMake helper、manifest 模板和 dev 打包脚本 |
| 2048 | 确定性模型、逐 tile 动画、原子持久化、撤销和矿物主题参考应用 |

## 安全原则

- 唯一应用/平台信任根是编译进生产二进制的官方 Ed25519 发布公钥。
- 私钥只允许在隔离的离线 signer 中使用；设备、AMR、仓库和普通 CI 均不持有私钥。
- development 包带明确 dev flag 且永不可安装；改扩展名不能绕过。
- payload 与双槽 anti-rollback policy 分根保存；卸载 payload 不清除高水位。
- 固定目录、no-follow、owner/mode/nlink、canonical encoding、逐文件 hash 和启动前复验共同失败关闭。
- sessiond 只向自己精确 fork 的 PID 发信号；没有 `pkill`/`killall`/Falcon termination 路径。
- 没有 secure/verified boot 或 TEE 时，已控制 root/内核仍可 patch 用户态 verifier；详见 [威胁模型](docs/security/threat-model.md)。

## 快速构建

Host contracts：

```powershell
cmake -S . -B build/host -DBUILD_TESTING=ON
cmake --build build/host --target lvgl_platform_contract_tests game_2048_model_tests game_2048_visual_test
ctest --test-dir build/host -C Release --output-on-failure
node --test tests/host/*.test.js launcher/test/*.test.js manager/test/*.test.js tools/lvapp/test/*.test.mjs
```

Windows 运行 contract test 时需让 OpenSSL 3 `libcrypto-3-x64.dll` 位于 `PATH`；MSVC/Ninja 构建应先进入 Visual Studio Developer Shell。

AArch64 targets：

```text
cmake -S . -B build/platform-aarch64 -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-aarch64.cmake
cmake --build build/platform-aarch64 --target \
  lvgl_sessiond lvgl_launcher lvgl_installer game_2048 lvgl_sdk_example
```

创建模板开发包：

```powershell
./sdk/package-app.ps1 `
  -Manifest sdk/template-app/app.json `
  -Executable build/platform-aarch64/sdk/template-app/lvgl-example `
  -Output example.lvapp.dev
```

`.lvapp.dev` 仅用于构建/格式验证。正式发布必须走 [离线发布流程](docs/release/release-process.md)。

## 文档

- [架构总览](docs/architecture/overview.md)
- [安全威胁模型](docs/security/threat-model.md) 与 [安全政策](SECURITY.md)
- [解析器 fuzz 与 sanitizer](docs/security/fuzzing.md)
- [SDK 快速开始](docs/sdk/quickstart.md) 和 [manifest 参考](docs/sdk/manifest-reference.md)
- [发布与认证设备操作流程](docs/release/release-process.md)
- [运行时排障](docs/troubleshooting/runtime.md)
- [后续代办](docs/ROADMAP.md)

## 设备边界

设备脚本只适用于已认证的 AArch64 有道词典笔，且全部强制提供精确 `-Serial` 和认证时记录的 `-ExpectedIdentitySha256`；任一不匹配都会在安装、卸载或状态读取前失败。当前附加的 Nexus 4 明确不在范围内，本工程不会对它执行任何设备操作。摘要参数防止操作员选错设备，不是 root-resistant attestation；生产 manager 仍独立使用内嵌 identity 和官方签名包失败关闭。
