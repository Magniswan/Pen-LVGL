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
- 商业发布私钥只允许在隔离的离线 signer 中使用；设备、AMR、仓库和普通 CI 均不持有私钥。个人无绑定发布的显式例外是所有者本机仓库外、受 NTFS ACL 保护的明文私钥，详见个人发布设计；它不具备商业密钥保护等级。
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

固定 Falcon AMR 构建入口（只构建/检查，不连接设备）：

```powershell
./scripts/build_launcher.ps1 -Production -NodeExecutable <node-18.20.8>
./scripts/build_manager.ps1 -NodeExecutable <node-18.20.8>
```

个人无绑定 manager 仍必须提供唯一官方公钥和正式已签名平台包；缺任一项即失败关闭。它不把设备 serial、机型、profile 或设备 identity 写入信任判断。

## 文档

- [架构总览](docs/architecture/overview.md)
- [安全威胁模型](docs/security/threat-model.md) 与 [安全政策](SECURITY.md)
- [官方签名密钥仪式](docs/security/key-ceremony.md)
- [解析器 fuzz 与 sanitizer](docs/security/fuzzing.md)
- [SDK 快速开始](docs/sdk/quickstart.md) 和 [manifest 参考](docs/sdk/manifest-reference.md)
- [发布与认证设备操作流程](docs/release/release-process.md)
- [运行时排障](docs/troubleshooting/runtime.md)
- [后续代办](docs/ROADMAP.md)

## 设备边界

设备脚本要求显式 `-Serial`，并把每条 ADB 命令固定到该 serial；serial 缺失、重复、offline 或 unauthorized 会在业务操作前失败。个人无绑定模式不把设备 identity、机型或 Falcon profile 作为 manager 的信任门禁。它仍只接受由内嵌官方 Ed25519 公钥验签的正式平台包；请仅在你本人已确认的设备上执行这些脚本。
