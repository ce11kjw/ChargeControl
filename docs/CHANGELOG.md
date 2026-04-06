# 更新日志

ChargeControl 的所有重要变更均记录于此。

---

## [v5.8] – 2026-04-06

### Changed
- fix: remove -lpthread -lm from Android NDK cross-compile step
- Initial plan
- Remove -lpthread from LDFLAGS (merged into libc in Android NDK API 21+)
- Initial plan
- fix: use quoted include for local sqlite3.h
- Fix Android build: download SQLite amalgamation instead of linking system library
- Initial plan
- build: switch workflow to Android NDK r27c cross-compilation
- Initial plan
- build: switch to Android NDK cross-compile (aarch64, API=35)
- fix: resolve sqlite3.h include error on macOS/Clang via pkg-config
- ci: improve ARM64 verification error message with file output details
- ci: add NDK cross-compilation steps to release workflow, add SKIP_COMPILE support in build.sh
- Initial plan
- Update build.sh with new content
- Update post-fs-data.sh with new initialization script
- chore: update CHANGELOG.md and update.json to v4.7 [skip ci]

---

## [v4.7] – 2026-04-06

### Changed
- feat: add datetime timestamps to logs, WebUI, and exports
- fix: build.sh auto-compiles charge_control and includes binary in module ZIP
- Initial plan
- Initial plan
- chore: update CHANGELOG.md and update.json to v4.6 [skip ci]

---

## [v4.6] – 2026-04-06

### Changed
- Rewrite Python backend in C11 with embedded HTTP server and bug fixes
- Initial plan
- fix: replace deprecated unescape() with TextEncoder in saveConfig base64 encoding
- fix: resolve 4 critical bugs in webroot/script.js causing UI to be completely unresponsive
- Initial plan
- Initial plan
- chore: update CHANGELOG.md and update.json to v4.5 [skip ci]

---

## [v4.5] – 2026-04-05

### Changed
- fix: remove server.py reference from flake8 lint step
- Initial plan
- feat: remove Flask server, add snapshot_daemon.py for KernelSU WebUI mode
- chore: update CHANGELOG.md and update.json to v4.3 [skip ci]

---

## [v4.3] – 2026-04-05

### Changed
- fix: merge conflicting .sidebar rules in mobile media query
- Initial plan
- chore: update CHANGELOG.md and update.json to v4.2 [skip ci]
- chore: update CHANGELOG.md and update.json to v4.2 [skip ci]

---

## [v4.2] – 2026-04-05

### Changed
- chore: update CHANGELOG.md and update.json to v4.2 [skip ci]

---

## [v4.2] – 2026-04-05

### Changed
- fix: extract inline Python heredoc to scripts/update_changelog.py to fix YAML syntax error
- Initial plan
- feat: add workflow_dispatch to build.yml for manual triggering
- Initial plan
- fix: auto-create git tag on workflow_dispatch in release.yml
- Initial plan
- fix: remove unused imports and add v*.* tag trigger for release workflow
- Initial plan
- translate docs and module.prop to Simplified Chinese
- chore: update CHANGELOG.md with missing versions and automate updates in release.yml
- Initial plan
- Initial plan
- chore: update update.json to v4.1 [skip ci]
- Initial plan

---

## [v4.1] – 2026-04-05

### Changed
- Fix launcher.py: install deps from requirements.txt then start server.py

---

## [v4.0] – 2026-04-05

### Changed
- feat: add KernelSU remote update support (update.json + workflow step)

---

## [v3.9] – 2026-04-04

### Changed
- Migrate WebUI to KernelSU native exec() API, remove Flask HTTP dependency
- fix: add launcher.py to REQUIRED and INCLUDE arrays in build.sh

---

## [v3.8] – 2026-04-04

### Changed
- docs: update frontend UI access URL to http://127.0.0.1:8080
- Fix service.sh: retry python detection loop, lock against duplicate starts

---

## [v3.7] – 2026-04-04

### Changed
- Update launcher.py to add --no-cache-dir flag to pip install command
- Update release.yml

---

## [v3.6] – 2026-04-04

### Changed
- Update GitHub Actions to Node.js 24 compatible versions
- 将项目所有内容翻译为简体中文
- Move frontend UI files to webroot/ and update server.py and build.sh

---

## [v3.5] – 2026-04-04

### Changed
- Add launcher.py, requirements.txt and improve service.sh Python detection

---

## [v1.0.3] – 2026-04-04

### Changed
- Add workflow_dispatch to release.yml; remove redundant upload-to-release.yml
- feat: implement advanced ChargeControl features - Web UI, multi-mode charging, statistics, CI/CD
- Add post-fs-data.sh script
- Create package.sh for packaging ChargeControl as KernelSU module

---

## [v1.0.1] – 2025-04-04

### 新增
- **现代化 Web UI** – 完整仪表板，支持深色/浅色主题、响应式布局（移动端与桌面端）
- **多种充电模式** – 标准、快充、涓流、省电、超级省电
- **实时电池监控** – 电量、温度、电压、电流
- **充电限制滑块** – 设置最大充电百分比（0–100%）
- **温度保护** – 根据可配置阈值自动降速/停止充电
- **充电统计** – 每日、每周、每月汇总数据，配合 Canvas 图表展示
- **电池健康评分** – 根据温度历史估算
- **CSV 与 JSON 数据导出**
- **配置编辑器** – 直接在 Web UI 中编辑 `config.json`
- **快速预设** – Night / Work / Travel / Save
- **SQLite 持久化** – 使用 `chargecontrol.db` 存储会话与快照
- **后台快照收集器** – 每 30 秒采样一次电池数据
- **`charge_control.py`** – 核心模块，包含 sysfs 读写辅助函数
- **`stats.py`** – 统计与分析模块
- **`server.py`** – 完整 REST API（Flask + CORS）
- **`config.json`** – 默认配置文件
- **`service.sh`** – 健壮的启动脚本，支持 Python 检测与 pip 安装
- **`uninstall.sh`** – 模块清理卸载脚本
- **`common.prop`** – 系统属性定义
- **GitHub Actions** – `build.yml`（CI）与 `release.yml`（自动发布）
- **`docs/API.md`** – REST API 参考文档
- **`docs/CONFIG.md`** – 配置指南
- **`.gitignore`** – 排除构建产物与数据库文件

### 变更
- `module.prop` – 版本升级至 `v1.0.1`，versionCode `101`
- `build.sh` – 完全重写：校验文件、设置权限、创建 ZIP 包
- `package.sh` – `build.sh` 的精简封装
- `README.md` – 完整项目文档

---

## [v1.0.0] – 2025-01-01

### 新增
- 初始发布
- 基础 Flask Web 服务器（`server.py`）
- 最简 HTML/CSS/JS 前端
- 用于 KernelSU 识别的 `module.prop`
