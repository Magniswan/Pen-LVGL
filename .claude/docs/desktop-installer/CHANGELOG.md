# Changelog - Desktop / Installer

> 最新变更在最上方。

## [2026-08-24] 从固定收件箱安装官方应用

**类型**: feat  
**提交**: 97990ea  
**风险**: HIGH

### 变更文件

| 文件 | 变更 | 说明 |
|---|---:|---|
| `CMakeLists.txt` | +15/-0 | 注册 installer、inbox 和 registry targets |
| `apps/installer/installer_ui.cpp` | +213/-0 | inspect/verify/install/result UI |
| `apps/installer/installer_ui.h` | +49/-0 | installer UI 状态 |
| `apps/installer/main.cpp` | +44/-0 | 内置安装器入口 |
| `apps/launcher/launcher_ui.cpp` | +4/-5 | 从动态 registry 创建 desktop cards |
| `apps/launcher/launcher_ui.h` | +3/-1 | desktop dynamic launch signature |
| `src/session/app_registry.cpp` | +104/-3 | 只读 FD registry 与 target fail-closed |
| `src/session/app_registry.h` | +1/-0 | stable-ID lookup |
| `platform/include/lvgl_platform/application_registry.h` | +43/-0 | registry schema |
| `platform/include/lvgl_platform/inbox_service.h` | +63/-0 | inbox service schema |
| `platform/src/services/inbox_service.cpp` | +329/-0 | 固定官方 inbox 安装服务 |
| `platform/src/session/application_registry.cpp` | +188/-0 | canonical registry codec |
| `tests/host/inbox-source.test.js` | +31/-0 | inbox 源码约束 |

### 影响范围

- **API**: 新增 inbox service 和 registry consumer。
- **跨模块**: platform-security、sessiond、manager。
- **数据模型**: app candidate token 与 canonical desktop registry。
- **配置**: 新增固定 application storage roots。

### 回滚指南

- 回滚：`git revert 97990ea`
- 检查：`apps/installer`、launcher UI、app registry、inbox service。
- 副作用：动态安装、桌面应用发现和普通应用启动全部移除。
