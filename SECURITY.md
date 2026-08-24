# Security Policy

## Supported state

当前仓库是预生产安全基线，没有已发布的商用版本或安全支持窗口。任何未设置正式官方公钥、未嵌入正式签名 payload、或使用 `holeSessionCertified=false` profile 的构建都不是生产构建。

## Reporting

发现签名绕过、anti-rollback 绕过、路径逃逸、TOCTOU、任意命令/路径注入、跨应用数据访问、可杀 Falcon/miniapp 的路径，或 production fail-open 行为时，请私下向项目安全负责人报告。报告应包含 commit、构建配置、最小复现、影响和是否需要 root；公开披露前先完成修复和 key/epoch 响应评估。

不要在报告中附带官方私钥、设备密钥或未脱敏的生产 identity。

## Key compromise response

1. 停止所有签名和发布。
2. 保存 signer/audit evidence，不在普通开发机复制私钥。
3. 生成新离线 key hierarchy；提升受影响应用的 `securityEpoch`。
4. 构建只信任新公钥的平台/manager，并通过受控固件更新部署。
5. 复核所有旧 counter/epoch high-water 与设备恢复路径。

当前 v1 只信任单一编译期官方 key，尚不支持设备端在线吊销；key rotation 需要正式平台更新。

## Non-claims

仅凭本仓库的用户态验签无法抵抗已控制内核的 root。商用材料不得使用“绝对不可破解”表述；应准确描述 verified boot/TEE 等实际部署条件和剩余风险。
