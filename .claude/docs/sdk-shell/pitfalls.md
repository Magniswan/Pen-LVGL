# SDK / Shell Pitfalls

1. 不要在构造函数中调用 LVGL；`lv_init` 尚未完成。
2. 不要从非 LVGL 主线程操作对象；后台结果应通过 timer/event 汇入主线程。
3. `destroy` 必须删除仍活动的 `lv_timer_t` 和 animation，避免 `lv_deinit` 后回调。
4. 不要调用 `exit()`/`exec()` 实现 HOME；发送类型化控制并让主循环自然退出。
5. 不要依赖硬编码 `/dev/dri`、evdev 或 session FD 数字；runtime 从已认证环境接管。
6. 不要把 `version` 当反回滚值；发布必须同时正确递增 `releaseCounter`。
7. 不要把 `.lvapp.dev` 改扩展名伪装成正式包；签名 envelope 和 manifest key ID 都会失败。
8. 不要把私钥传给 CMake、manager、设备或 CI 普通构建；签名是隔离的发布步骤。
9. 私有状态只用 `RuntimeContext.storage` 固定记录；不要解析 `LVGL_APP_STORAGE_FD`、拼接路径或绕过单记录上限。
10. signed manifest 的 `maxFiles`/`dataMiB` 是总 quota，调用参数 `maximum_size` 是更小的单记录上限；两者都不能把 storage 当大文件仓库。
11. 不要包含 `src/runtime/*.h`、`src/shell/*.h` 或复制类声明；只用安装出的 `lvgl_platform/*.hpp`，否则内部重构可能造成 ODR/ABI 分裂。
