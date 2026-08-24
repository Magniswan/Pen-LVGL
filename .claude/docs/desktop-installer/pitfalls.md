# Desktop / Installer Pitfalls

## 不要信任文件名

文件名只用于安全地打开目录项。身份来自已验签 manifest，UI 选择使用完整包 digest；禁止按名称、扩展名或外部 metadata 放行。

## 不要缓存“已验证”结论

扫描用于展示，安装必须重读、重验签、重跑高水位策略。即便目录权限正确，也不能把 `InboxCandidate.installable` 当成最终授权。

## Registry 不是启动授权

桌面数据来自 sessiond，但桌面进程仍可能被 root 修改。launch 请求只能携带 canonical app ID；sessiond 必须在每次启动前重新验证磁盘状态和包。

## Root 对手的现实边界

root 可以替换用户态二进制、内核或内存。没有 Secure Boot/verified boot/TEE 时，用户态无法保证自身没有被 patch。本模块能提供 fail-closed 校验、回滚防护、缩小 TOCTOU 和可审计状态，不能宣称抵抗已控制内核的攻击者。

## UI 不提供逃生开关

不要加入“忽略签名”“自定义公钥”“自定义目录”或开发模式按钮。开发包只能在独立的开发构建/工具链中使用，生产安装器始终只信任官方密钥。

## 尚未完成

卸载、隔离、回滚管理 UI 仍是后续任务；实现时也必须由固定服务完成，不能让 UI 直接删除路径。
