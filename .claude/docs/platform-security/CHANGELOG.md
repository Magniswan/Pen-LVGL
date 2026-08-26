# Changelog - Platform Security

> 最新变更在最上方；排查签名、安装或状态问题时优先阅读。

## [2026-08-26] 增加 token-bound 手动 rollback/remove

**类型**: security
**提交**: 0c4de06
**风险**: HIGH

### 安全性质

- installed snapshot token 绑定 payload directory identity 与完整 policy state，变更前重新枚举匹配。
- rollback 重验 official previous、隔离 current、保留 release/security high-water，且不增加 launch failure。
- remove 原子隔离 payload、同设备有界 no-follow 清理，保留 policy 与 private data。
- 独立 audit 记录 BEGIN/COMMIT/ISOLATED；UI 协议仍不包含路径、key 或 caller policy。

### 回滚指南

- 回滚：`git revert 0c4de06`；必须同步回滚 installer v2 client/UI/protocol。
- 副作用：失去设备内受控恢复/卸载能力，但不会清理已存在 policy/data/audit。

## [2026-08-24] 增加 parser fuzz 与 sanitizer CI 基线

**类型**: security-test
**提交**: 55850f7
**风险**: MEDIUM

### 安全性质

- 三个 libFuzzer target 直接覆盖生产 package/state/session 解析入口。
- fuzz build 强制 Clang + ASan + UBSan；不能静默降级为未插桩运行。
- encoder 生成有效深层 seeds，现有 signed/dev package vectors 进入 package corpus。
- workflow 限制 time/length/RSS，失败保留 crash artifact，官方 actions 固定 commit SHA。
- 当前只有严格交叉编译/YAML/host regression 证据；首次远端 sanitizer fuzz 仍未执行，不把 workflow 配置等同于测试通过。

### 回滚指南

- 回滚：`git revert 55850f7`
- 副作用：移除持续 parser 内存安全/UB 探测，不影响目标二进制默认构建。

## [2026-08-24] 强制应用隔离与存储配额

**类型**: security
**提交**: bac2474
**风险**: HIGH

### 安全性质

- 验签清单保留 memory/CPU/maxFiles/data limits，sessiond 不再丢弃已认证限制。
- 每个前台 app 使用碰撞检测的独立非 root UID/GID、不可 dump、no-new-privs 与 rlimit。
- AArch64 runtime 强制 seccomp，禁止网络/进程/挂载/写路径/exec mapping，并把 DRM ioctl 缩到运行期 3 项。
- storage.private 改为 root-owned `SOCK_SEQPACKET` broker，强制 record bounds、owner/mode/nlink/type、`maxFiles`/`dataMiB` 和 fsync+rename。
- 不改变 root/内核攻击边界；cgroup、只读 mount namespace 和目标内核认证仍未完成。

### 回滚指南

- 回滚：`git revert bac2474`
- 检查：回滚会恢复目录 FD 直传并取消 quota/non-root/seccomp，不应发布为安全更新。
- 数据：不要删除 `/userdisk/apps/lvgl-data`；broker 会清理合法的旧 `.write-*` 临时文件。

## [2026-08-24] 封闭应用私有记录路径

**类型**: security
**提交**: 0f196ed
**风险**: HIGH

### 安全性质

- sessiond 只为有效 app ID 和 `storage.private` 创建 root-owned 0700 目录。
- 应用只继承精确 dirfd；环境不包含 caller-selected path、root 或 key。
- record read/write 使用 `openat`/`O_NOFOLLOW`、0600、owner/nlink/type/bounds 检查与 fsync+rename。
- root/内核攻击者仍可 patch 进程；独立 UID、namespace 和 quota 未完成，不能据此宣称 root 隔离。

### 回滚指南

- 回滚：`git revert 0f196ed`
- 检查：`lvgl-data` 是保留数据目录；回滚不应擅自删除用户状态。
- 副作用：所有声明 private storage 的应用将得到 unavailable 状态。

## [2026-08-24] 安装固定收件箱中的官方应用

**类型**: feat  
**提交**: 97990ea  
**风险**: HIGH

### 变更文件

| 文件 | 变更 | 说明 |
|---|---:|---|
| `platform/include/lvgl_platform/application_registry.h` | +43/-0 | registry 公开 schema 与 bounds |
| `platform/include/lvgl_platform/inbox_service.h` | +63/-0 | 固定 inbox 公开 API |
| `platform/include/lvgl_platform/version.h` | +2/-0 | 平台/SDK ABI 版本常量 |
| `platform/src/services/inbox_service.cpp` | +329/-0 | 固定目录扫描、SHA-512 token、验签与二次校验 |
| `platform/src/session/application_registry.cpp` | +188/-0 | canonical 应用注册表 |
| `platform/src/session/session_daemon.cpp` | +217/-20 | 动态 release 复验与 FD 启动 |
| `platform/src/storage/state_store.cpp` | +23/-5 | 支持拆分 payload/policy 根 |
| `CMakeLists.txt` | +15/-0 | 注册安全服务与 installer target |

### 影响范围

- **API**: 新增 inbox service 与 application registry API。
- **跨模块**: desktop、installer、manager、sessiond 同步变化。
- **数据模型**: 新增 desktop registry 与每应用 policy state。
- **配置**: 固定 `/userdisk/apps/lvgl-{apps,app-policy,inbox}`。

### 回滚指南

- 回滚：`git revert 97990ea`
- 检查：上述 platform 文件及 `apps/installer`、`src/session/app_registry.cpp`。
- 副作用：会移除设备端普通应用安装与动态桌面功能。

## [2026-08-24] 强制官方信任与反回滚策略

**类型**: feat  
**提交**: 81fa3d2  
**风险**: HIGH

### 变更文件

| 文件 | 变更 | 说明 |
|---|---:|---|
| `platform/src/package/rollback_policy.cpp` | +133/-0 | counter、epoch、digest 高水位策略 |
| `platform/src/trust/trust_store.cpp` | +51/-0 | 编译期官方 Ed25519 公钥信任槽 |
| `platform/include/lvgl_platform/rollback_policy.h` | +61/-0 | 安装策略公开类型 |
| `platform/include/lvgl_platform/trust_store.h` | +25/-0 | 官方 trust store 接口 |
| `platform/include/lvgl_platform/official_key_config.h.in` | +10/-0 | 生成式公钥配置头 |
| `tests/host/platform_contract_test.cpp` | +111/-0 | trust/rollback 契约测试 |
| `CMakeLists.txt` | +33/-0 | 生产 key 格式和 fail-closed 配置 |

### 影响范围

- **API**: 新增 official trust store 与 install policy。
- **跨模块**: 所有平台/应用包安装链。
- **数据模型**: 引入 release counter 与 security epoch 高水位。
- **配置**: 生产构建必须提供非测试官方公钥。

### 回滚指南

- 回滚：`git revert 81fa3d2`
- 检查：trust store、rollback policy、生成的官方 key header。
- 副作用：回滚会取消生产信任根和反回滚保障，不应发布。
