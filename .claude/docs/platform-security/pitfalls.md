# Platform Security Pitfalls

1. 不要把 SHA/CRC 当作真实性证明；只有编译官方根验证的 Ed25519 决定发布身份。
2. 不要为“开发方便”给生产 API 增加公钥、路径、shell、force 或 allow-development 参数。
3. 不要在验签前展示 manifest 身份为“可信”；无效包的名称仅能标为不可识别数据。
4. 不要把 inbox 文件名拼进绝对路径；保持目录 FD + `openat(O_NOFOLLOW)`。
5. 不要只校验 `.package.lvapp` 而直接按磁盘入口路径执行；必须比对 current digest、release counter 和每个安装文件。
6. 不要把 policy state 放在会随卸载清空的 payload root，否则 root 用户可通过卸载/重装回滚。
7. `loaded_degraded` 表示可恢复但冗余不足，应向运维暴露；`split_brain` 不可自动选边。
8. 文件 mode、owner、nlink、size 与内容都是启动不变量；只检查 hash 文件或 mtime 不够。
9. 生产公钥不是秘密，隐藏它不会提升安全；必须保护的是离线私钥和发布流程。
10. 面对已取得 root 且设备无验证启动的攻击者，不得宣传“防破解完成”；应明确软件边界并规划 secure boot/TEE。
11. 不要把 app-private 目录 FD 重新交给应用；否则应用能绕过 broker quota、记录语法和原子提交策略。
12. 手动 lifecycle mutation 不能只按 app ID；必须重算并匹配 installed snapshot token，否则会重新引入选择—执行 TOCTOU。
13. payload remove 不等于清除 policy/data；删除 high-water 会允许旧正式包回滚，删除 private data 也超出卸载授权。
