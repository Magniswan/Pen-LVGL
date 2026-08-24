# Falcon Data Model

## `ManagerSnapshot`

| 字段 | 含义 |
|---|---|
| `success` | 本次事务成功提交 |
| `available` | manager build、payload、device identity 均可信 |
| `payloadAvailable` | 构建时嵌入了平台包 |
| `installed` | policy 指定 release 目录存在 |
| `repairRequired` | state 退化、payload 缺失或 current 不匹配 |
| `updateAvailable` | 内嵌包 counter 高于当前 release |
| `busy` | 固定事务正在执行 |
| versions/profile | 当前、内嵌版本与认证 profile |
| `code/detail` | 稳定机器码与可读诊断 |

JS 只接受字段的精确类型，不使用 truthy coercion；异常 native 返回会归一为 `MANAGER_INVALID_RESPONSE`。

## Session status

launcher 从最大 4096 bytes、root-owned、不可组/其他用户写入的固定状态文件读取键值。关键字段包括 `SESSION_PID`、`SESSION_NONCE`、`STATE`、`RESULT`、`HOLE_READY`、`INPUT_READY`、`LOGICAL_WIDTH`、`LOGICAL_HEIGHT`。

PID 只有在进程存在且 `/proc/PID/exe` 精确指向固定 sessiond 路径时有效。nonce 将触控 sequence 绑定到一次会话。

## Falcon touch ownership

页面最多维护 32 个同时活动触点。hide/cancel 会为所有已分配 slot 发送 cancel 并清空本地映射，避免下次显示继承卡住的 pointer 状态。
