# Youdao Dictionary Pen LVGL Platform

## 概述

面向有道词典笔的离线优先 LVGL 应用平台。它由两个 Falcon AMR、原生 KMS/LVGL 会话、仅官方签名的 `.lvapp` 安装链、桌面/安装器和参考应用组成。

## 构建

- C/C++：C++17、C11、CMake 3.20+，目标 ABI 为 AArch64/glibc。
- UI：仓库内 LVGL；目标 DRM 依赖来自 `device-sysroot`。
- Falcon：Node/pnpm + HaaSUI/Falcon 工具链；原生插件由 ARM GNU Toolchain 11.3 构建。
- 生产构建必须设置 `LVGL_PLATFORM_PRODUCTION_BUILD=ON` 和 64 位小写十六进制 Ed25519 官方公钥；私钥不得进入仓库或设备构建。
- 常用命令和精确环境见 [配置与构建](docs/config/README.md)。

## 测试环境

- 主机契约：Windows/MSVC + Node.js。
- 目标编译：WSL 内 AArch64 GNU 工具链。
- 真机认证只接受已记录的词典笔 profile；当前连接的 Nexus 4 不是目标设备，禁止对其执行安装或写操作。
- 开发构建默认无官方密钥并故意拒绝生产安装。

## 项目结构

```text
platform/       安全契约、包/存储、会话守护、收件箱
src/            DRM/输入运行时、应用控制、共享 Shell
apps/           桌面、官方安装器、2048 等原生应用
launcher/       Falcon 持久 hole 启动器 AMR
manager/        Falcon 平台安装/修复/升级/卸载 AMR
tools/lvapp/    确定性打包、离线签名、检查与验证
profiles/       设备证据与认证状态
scripts/        主机构建和设备操作（必须显式目标）
tests/          原生契约、模型和安全源码测试
docs/           面向开发者、发布者和运维者的正式文档
```

## 模块索引

| 层级 | 模块 | 说明 | 文档 |
|---|---|---|---|
| 基础 | platform-security | 官方信任、LVAPP、反回滚、无跟随存储 | [索引](docs/platform-security/README.md) |
| 基础 | runtime | KMS overlay、Falcon 转发输入、子进程协议 | [索引](docs/runtime/README.md) |
| 基础 | sdk-shell | AppControl、Shell、主题与应用 ABI | [索引](docs/sdk-shell/README.md) |
| 业务 | desktop-installer | 动态桌面注册表与四阶段安装器 | [索引](docs/desktop-installer/README.md) |
| 业务 | game-2048 | 确定性模型、动画与持久化 | [索引](docs/game-2048/README.md) |
| 业务 | falcon | manager 与不杀 miniapp 的 hole launcher | [索引](docs/falcon/README.md) |
| 打包 | release-tooling | `.lvapp`、AMR、发布与检查脚本 | [索引](docs/release-tooling/README.md) |
| 配置 | config | profiles、构建开关、环境和设备边界 | [索引](docs/config/README.md) |

## 核心架构

```text
Falcon miniapp + <hole> ─touch datagram─> lvgl-sessiond (root trust boundary)
                                           ├─ reverify signed platform release
official .lvapp -> fixed inbox -> installer ├─ derive read-only desktop registry
                         │                  ├─ quota storage broker + inherited DRM FD
                         │                  └─ fexecve one non-root rlimit/seccomp child
                         └─ payload releases + separate dual-slot anti-rollback state
```

## 模块依赖关系

- `launcher` 只启动固定 `lvgl-sessiond --falcon-hole`，从不终止 Falcon/miniapp。
- `manager` 与 `apps/installer` 只能调用编译进二进制的官方信任根。
- `desktop` 只消费 sessiond 传入的注册表 FD；应用 ID 从不解释为路径或命令。
- 普通应用依赖 `platform_runtime`、`app_shell` 和稳定 SDK ABI，不直接拥有 Falcon IPC。
- `tools/lvapp` 是唯一生成签名封装的离线工具；设备侧没有签名能力。

## 环境配置

- 生产公钥、认证 profile/machine 和 manager 设备指纹都是构建时输入。
- `profile.env` 必须包含在官方签名平台包中；未知字段或未认证 hole 会失败关闭。
- 运行时仅接受 sessiond 设置的固定 FD 和显示参数。详见 [配置文档](docs/config/README.md)。

## 重要坑点

1. Root 攻击者在缺少 secure boot/TEE 时可补丁运行时代码；本项目只能实现强校验、隔离和篡改证据，不能宣称不可破解。
2. CloudBrowser 的 plane/矩形只是研究证据，不是设备认证；未认证 profile 必须拒绝启动。
3. 开发 `.lvapp.dev` 永远不能安装，RFC 8032 测试密钥也不能进入官方密钥槽。
4. 载荷和反回滚策略根必须分离；卸载载荷不得清空 high-water mark。
5. 不得从 UI、Falcon JS、环境或文件名接受可执行路径、shell 命令或替代公钥。
6. 每次启动动态应用都必须重新验签、比对当前状态摘要并逐文件复测。
7. 会话只可向自己精确 fork 的子 PID 发信号；禁止 `pkill`、`killall` 或按名称终止。
8. 当前目标 profile 未经真机认证，构建成功不能等同于硬件兼容。
9. AppStorage 只允许使用 SDK broker API；把目录 FD 交给应用会绕过 quota 和记录策略。
10. parser 修改必须同步运行 contract tests 与对应 fuzz harness；短时无 crash 不能表述为安全证明。

## 模块变更日志

每个模块的 `CHANGELOG.md` 记录相关提交、风险、验证与回滚方式。排查回归时先读该模块日志，再对照根 Git 历史。

## 文档索引

模块 README 均链接到 API、数据模型、坑点和变更日志。正式开发者文档位于根 `docs/`；本目录是给 AI/自动化的短索引，二者必须交叉引用并保持一致。
