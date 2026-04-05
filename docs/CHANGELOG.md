# 更新日志

ChargeControl 的所有重要变更均记录于此。

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
