# Desktop and On-device Installer

## 概述

`apps/launcher` 是会话内桌面，`apps/installer` 是只接受官方签名包的设备端安装器。二者都不是信任根：sessiond 生成只读应用注册表，安装服务固定使用编译期官方公钥和固定目录。

## 依赖

- [Platform Security](../platform-security/README.md)
- [Device Runtime](../runtime/README.md)
- [SDK / Shell](../sdk-shell/README.md)

## 主流程

```text
/userdisk/apps/lvgl-inbox/*.lvapp
  -> 固定目录安全扫描
  -> 官方签名、manifest、平台策略、高水位检查
  -> UI 以 SHA-512 token 选择
  -> 安装前重新扫描和重新验证
  -> 双槽原子安装 + policy state
  -> 回到 desktop
  -> sessiond 重新构建只读 registry
```

桌面只提交 stable app ID，不持有可执行路径。sessiond 再次验证已安装内容后，以已打开的 entry FD 启动应用。

## 安全边界

- 唯一包信任根是编译进二进制的官方发布公钥。
- 收件箱、应用和策略根路径均不可由调用者指定。
- 包名、文件名和桌面元数据都不构成授权。
- `top.lvgl.platform`、`top.lvgl.desktop`、`top.lvgl.installer` 不允许由普通应用包覆盖。
- 目标设备拿不到有效 registry FD 时仅显示桌面自身，不回退到开发应用列表。

## 详细文档

- [api-inbox.md](api-inbox.md)：扫描、安装和 registry API。
- [data-model.md](data-model.md)：候选、token、注册表与安装状态。
- [pitfalls.md](pitfalls.md)：TOCTOU、root 威胁和 UI 约束。
- [CHANGELOG.md](CHANGELOG.md)：变更与验证。
