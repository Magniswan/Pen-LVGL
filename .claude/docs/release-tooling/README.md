# Release Tooling

## 概述

`tools/lvapp` 生成确定、canonical 的应用包，执行离线 Ed25519 签名和独立验证；`scripts` 与两个 AMR 的 native build scripts 负责目标编译和有边界的 AMR 安装。设备没有签名能力。

## `.lvapp` 发布流

```text
app.json + app-root/
  -> lvapp build -> reproducible .lvapp.dev (永不可安装)
  -> offline isolated signer -> lvapp sign -> .lvapp
  -> independent public-key verify
  -> release manifest/hash publication
  -> copy to fixed device inbox
```

命令：

```text
node tools/lvapp/cli.mjs build --manifest app.json --root root --out app.lvapp.dev
node tools/lvapp/cli.mjs sign --input app.lvapp.dev --key private.pem --out app.lvapp
node tools/lvapp/cli.mjs inspect --input app.lvapp
node tools/lvapp/cli.mjs verify --input app.lvapp --public-key public.pem
```

正式流水线必须把 `sign` 放在离线隔离环境；开发机和 CI 构建节点只生成 `.lvapp.dev`。官方私钥不得出现在环境变量、命令日志、仓库、AMR、平台包或设备。

## AMR 发布

- launcher native plugin：固定 AArch64 toolchain 构建 `libjsapi_lvgl_launcher.so`，随后 Falcon CLI 产 AMR。
- manager native plugin：生产构建嵌入已签名平台 `.lvapp` 和认证配置，随后产独立 AMR。
- host install scripts 只安装对应 AMR，不写入/删除 native platform payload。

## 发布门禁

- Node LVAPP tests、host contract/model tests、AArch64 全 targets、Falcon source/state tests 全通过。
- production public key 与包 signer key ID 一致；独立 verifier 成功。
- profile 已真机认证，manager device identity 与目标批次一致。
- artifacts 带 SHA-256、版本、release counter、security epoch 和可复现构建记录。
- host device scripts 必须提供认证 serial/identity；所有业务命令被强制 pin 到 serial，并在业务操作前复算 identity。
- operator-provided identity 只防误操作；正式发布仍需 manager 内嵌 identity、官方签名包和可信启动链。

## 详细文档

- [api-lvapp.md](api-lvapp.md)：manifest 和 CLI 约束。
- [data-model.md](data-model.md)：binary layout、manifest 与签名 envelope。
- [pitfalls.md](pitfalls.md)：密钥、可复现性与设备发布注意事项。
- [CHANGELOG.md](CHANGELOG.md)：工具链变更。
- [正式发布流程](../../../docs/release/release-process.md)：设备 identity 计算、脚本参数和发布门禁。
