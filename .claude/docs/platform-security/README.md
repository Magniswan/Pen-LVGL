# Platform Security 基础模块

## 职责

`platform/include/lvgl_platform` 与对应 `platform/src` 定义所有生产安全决策：官方根密钥、`.lvapp` 解析、兼容策略、反回滚状态、无跟随解包、固定收件箱和桌面注册表编码。业务 UI 只能展示结果，不能放宽结果。

## 信任边界

- 唯一软件发布信任是编译进二进制的 Ed25519 官方公钥。
- `OfficialTrustStore::compiled()` 不接受调用方密钥；未配置的开发构建失败关闭。
- RFC 8032 测试公钥、全零公钥和格式错误公钥在生产配置阶段被拒绝。
- 私钥只允许存在于离线发布环境，不属于设备、AMR 或仓库内容。
- Root 在无 secure boot/TEE 时仍可改写正在执行的代码；这里提供的是最大化软件防护和篡改检测，不是不可破解承诺。

## 核心流程

```text
fixed inbox regular FD
  -> SHA-512 token
  -> official Ed25519 verification
  -> canonical manifest + per-file SHA-256
  -> ABI/profile/machine/capability policy
  -> release/security high-water check
  -> no-follow staged extraction + fsync + rename-no-replace
  -> redundant state.a/state.b commit in separate policy root
  -> sessiond launch-time full remeasurement
```

## 固定目录

| 目录 | 内容 | 卸载策略 |
|---|---|---|
| `/userdisk/apps/lvgl-platform` | 平台发行版 | manager 可移除 |
| `/userdisk/apps/lvgl-platform-policy` | 平台 high-water state | 保留 |
| `/userdisk/apps/lvgl-apps` | 应用 releases | 独立管理 |
| `/userdisk/apps/lvgl-app-policy` | 应用 high-water state | 不随载荷清除 |
| `/userdisk/apps/lvgl-inbox` | 只读扫描收件箱 | 文件名不受信任 |

目录必须 root-owned 且 group/other 不可写。所有向下遍历使用目录 FD、`openat` 与 `O_NOFOLLOW`；应用 ID 只经过闭合语法验证后作为单个目录分量。

应用永远拿不到上述 storage 目录 FD。sessiond 通过 canonical broker 协议代理固定 record，并依据已验签 manifest 强制 `maxFiles`/`dataMiB`；任何未知目录项、owner/mode/nlink/type/size 异常都会把存储视为损坏并失败关闭。

## 对外 API

- 包、策略、安装与收件箱方法见 [api-package-storage.md](api-package-storage.md)。
- 二进制 manifest、release state、registry 模型见 [data-model.md](data-model.md)。
- 安全限制和常见误用见 [pitfalls.md](pitfalls.md)。
- 变更与回滚见 [CHANGELOG.md](CHANGELOG.md)。

## 稳定失败原则

- 每层返回枚举状态和机器可读的全大写 `detail`；UI 才映射中文。
- 未知字段、尾随字节、歧义 profile、降级、同序号异内容和状态 split-brain 全部拒绝。
- `development.lvapp.dev` 可供主机检查，但生产 verifier/installer 永远拒绝。
- 状态单槽损坏可进入显式 degraded 恢复；同 generation 不同内容是 split-brain，必须失败关闭。

## 验证入口

- `lvgl_platform_contract_tests`：密码学向量、包损坏、策略、状态、注册表。
- `tools/lvapp/test`：Node 构建/签名/检查的正负向量。
- `tests/host/inbox-source.test.js`：固定根、摘要令牌、无调用方路径/密钥。
- `tests/fuzz/*`：package/state/session 的生产 parser harness、seed generator 与 dictionaries。
- `.github/workflows/security-fuzz.yml`：Clang ASan/UBSan contracts 和 bounded libFuzzer smoke；当前仍需首次远端运行证据。
- 详细运行方法与结论边界见 [根 fuzz 文档](../../../docs/security/fuzzing.md)。
