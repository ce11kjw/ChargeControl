# 🔋 ChargeControl

> 基于 KernelSU 的高级电池充电控制模块，提供科技感 Web UI、实时数据推送、多充电模式与完整统计分析。

[![Build & Release](https://github.com/ce11kjw/ChargeControl/actions/workflows/build.yml/badge.svg)](https://github.com/ce11kjw/ChargeControl/actions)
[![Version](https://img.shields.io/badge/version-v4.8.5-blue.svg)](https://github.com/ce11kjw/ChargeControl/releases)
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)

---

## ✨ 功能特性

### 🔮 科技感 UI
- 深色主题 + 玻璃拟态设计
- 粒子背景动画
- 扫描线效果
- 响应式布局（手机/平板/电脑）
- 亮色/暗色主题切换

### ⚡ 实时数据推送
- SSE（Server-Sent Events）实时推送
- 数据变化立即更新，无变化不推送
- 页面不可见时自动断开，节省电量

### 🎬 智能帧率系统
- 0~165fps 动态调整
- 用户操作时：0fps → 设备最高帧率
- 用户停止后：最高帧率 → 0fps
- 完美适配高刷屏幕

### 🔋 电池信息显示
- **核心数据**：电量、状态、温度、电压、电流、功率、健康度
- **电芯信息**：设计容量、当前容量、循环次数、制造商、电池技术
- **芯片信息**：芯片型号、CPU核心数、CPU频率、芯片温度
- **充电器信息**：充电器类型、快充协议、最大功率
- **历史数据**：今日充电次数、耗电、平均功耗、电池损耗

### 🌡️ 温度监控
- 电池温度
- CPU 温度
- GPU 温度
- 主板温度

### ⚡ 多充电模式
| 模式 | 说明 | 功率 |
|------|------|------|
| 🚀 涡轮加速 | 最大速度充电 | 65W |
| 🤖 智能模式 | AI 自动调节 | 自适应 |
| 🔋 标准充电 | 日常使用 | 20W |
| 💧 涓流充电 | 保护电池 | 5W |
| 🛡️ 电池保护 | 最大程度保护 | 3W |

### 📊 数据统计
- 充电曲线图表
- 温度趋势图
- 每日/每周/每月统计
- CSV/JSON 数据导出

### 🔧 智能功能
- 充电上限设置（0-100%）
- 温度保护（自动降速/停止）
- 定时充电
- 充电完成通知

---

## 📱 兼容性

### Root 方案
| 方案 | 状态 |
|------|------|
| KernelSU (KSU) | ✅ 完美支持 |
| Magisk | ✅ 完美支持 |
| APatch | ✅ 完美支持 |
| SuperSU | ✅ 完美支持 |

### 芯片平台
| 平台 | 状态 |
|------|------|
| 高通 Snapdragon | ✅ 完美支持 |
| 联发科 MediaTek | ✅ 完美支持 |
| 三星 Exynos | ✅ 完美支持 |
| 华为麒麟 | ✅ 完美支持 |
| Google Tensor | ✅ 完美支持 |
| 紫光展锐 | ✅ 完美支持 |

### ROM 系统
| ROM | 状态 |
|-----|------|
| 原生 Android/AOSP | ✅ 完美支持 |
| MIUI / HyperOS | ✅ 完美支持 |
| ColorOS | ✅ 完美支持 |
| OriginOS | ✅ 完美支持 |
| OneUI | ✅ 完美支持 |
| LineageOS | ✅ 完美支持 |

---

## 📦 安装说明

### 前提条件
- 已安装 **KernelSU** 或其他 Root 方案
- Android 8.0+

### 安装步骤
1. 从 [Releases](https://github.com/ce11kjw/ChargeControl/releases) 下载最新版本
2. 打开 **KernelSU 管理器** → **模块** → **从存储安装**
3. 选择下载的 `ChargeControl_v*.zip` 文件
4. 重启设备

### 访问 Web UI
浏览器访问：`http://127.0.0.1:8080`

---

## 🔨 从源码编译

### 环境要求
- Linux/macOS
- Android NDK
- GCC 或 Clang

### 编译步骤
```bash
# 克隆仓库
git clone https://github.com/ce11kjw/ChargeControl.git
cd ChargeControl

# 编译 C 代码（交叉编译 ARM64）
make CC=aarch64-linux-android33-clang

# 打包 ZIP
bash build.sh

# 输出: out/ChargeControl_v*.zip
```

---

## 📊 数据读取

### 自动发现
模块启动时会自动扫描 `/sys/class/power_supply/` 目录，动态发现所有电池设备。

### 双电芯支持
自动识别双电芯设备，分别显示每个电芯的状态。

### 严格数据验证
- 读取成功的数据：显示在 UI
- 读取失败的数据：隐藏 UI + 记录日志
- 格式异常的数据：隐藏 UI + 记录原因

---

## 📝 更新日志

### v4.8.5 (2024-07-29)
- ✨ 全新科技感 UI 设计
- ⚡ SSE 实时数据推送
- 🎬 智能帧率系统 (0~165fps)
- 🔋 动态电池路径发现
- 📱 双电芯支持
- 🌡️ 多温度监控
- 📊 完整数据验证
- 🛡️ 读取失败日志记录

---

## 📄 许可证

MIT License - 详见 [LICENSE](LICENSE)

---

## 🙏 致谢

- [KernelSU](https://kernelsu.org/) - 内核级 Root 方案
- [cJSON](https://github.com/DaveGamble/cJSON) - JSON 解析库

---

**⭐ 如果这个项目对您有帮助，请给个 Star 支持一下！**
