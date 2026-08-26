# Package / Storage API

## Crypto 与信任

| API | 行为 |
|---|---|
| `CryptoProvider::load_default()` | 动态加载受支持 libcrypto；缺少符号返回空 |
| `sha256` / `sha512` | 对显式字节范围计算摘要 |
| `verify_ed25519` | 验证原始 32-byte Ed25519 公钥签名 |
| `OfficialTrustStore::compiled()` | 返回唯一编译时官方根；没有导入 API |
| `OfficialTrustStore::verify()` | 生产模式完整验证 `.lvapp` |

## Package

`verify_package(bytes, size, crypto, trusted_keys, allow_development)` 是底层测试/检查入口。生产业务必须调用 `OfficialTrustStore::verify()`，不得构造 `trusted_keys`。

成功结果 `PackageVerification` 同时要求：`status == verified`、`development == false`、`signature_verified == true`。仅检查 `ok()` 后仍要遵守调用场景的 development 约束。

## Policy

`evaluate_install_policy(candidate, digest, context, high_water)` 依次检查：

1. 已认证生产包；
2. SDK ABI 与最低平台版本；
3. profile 与 machine；
4. capability 闭合 allowlist；
5. app ID 和 signing key 所有权；
6. security epoch 与 release counter 单调性；
7. 同 counter 必须是完全相同的 SHA-512 包。

`advance_high_water_mark` 是防御式纯函数；输入不满足单调条件时保留旧值。

## Transaction

| API | 用途 |
|---|---|
| `stage_verified_release` | 无跟随解包到 `apps/<id>/releases/<counter>` |
| `activation_state_for` | 生成 current/previous/high 的下一 generation |
| `persist_release_state` | 随机临时文件、fsync、原子替换两个冗余槽 |
| `install_official_package_with_state_root` | 组合验签、策略、解包和独立 policy root 提交 |

事务先提交不可变 release，再提交状态。中断可能留下未激活 release，但不会让未提交状态指向半成品；相同官方包重试是幂等的。

## Inbox

| API | 调用约束 |
|---|---|
| `prepare_application_storage()` | 只创建三个编译固定的 0700 根目录 |
| `scan_official_inbox(context, crypto)` | 扫描 regular/nlink=1 文件，文件名不进入授权语义 |
| `install_official_inbox_candidate(token, ...)` | token 必须是返回的 128 位小写 SHA-512；重新扫描和验签 |

收件箱服务没有路径、公钥或 “allow development” 参数。扫描结果的 `installable` 只是预检；安装仍执行完整事务，不能绕过最终策略。

## 已安装生命周期

sessiond 的 installed snapshot 枚举从固定 payload/policy roots 读取真实状态，生成绑定 directory device/inode/ctime、state generation/releases/high-water/digests/key ID 的 SHA-512 token。`rollback(token)` 和 `remove(token)` 都在变更前重新枚举并精确匹配：

- rollback：重新打开并官方验签 previous package、逐文件复测、重跑当前 profile/policy，然后通过 `rollback_release(active, false)` 持久化新双槽 state；
- remove：先把 payload app directory 原子 rename 到随机 tombstone，再进行 no-follow、同设备、深度/节点有界清理；policy 与 private data 不在删除 root；
- audit：在独立 root-owned policy audit directory 记录有界 0600 `BEGIN/COMMIT/ISOLATED` 行并原子轮转。

这些是 sessiond 内部特权服务，不是应用 SDK。调用方不能提供 app ID、路径、release、digest、policy 或公钥来替代 snapshot。
