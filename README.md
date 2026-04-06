# 充电控制 ⚡

> 基于 KernelSU 的 Android 电池充电控制高级模块，提供现代化 Web 界面、多充电模式、实时统计及自动化 CI/CD。

[![构建与测试](https://github.com/ce11kjw/ChargeControl/actions/workflows/build.yml/badge.svg)](https://github.com/ce11kjw/ChargeControl/actions/workflows/build.yml)

---

## 功能特性

| 功能 | 说明 |
|------|------|
| 🌐 **Web 仪表盘** | 支持暗色/亮色主题的现代响应式界面 |
| ⚡ **多充电模式** | 普通、快充、涓流、省电、超级省电 |
| 📊 **统计分析** | 每日/每周/每月图表，支持 CSV 和 JSON 导出 |
| 🌡️ **温度保护** | 过热时自动降速或停止充电 |
| 🎯 **充电上限** | 设置最大充电百分比（如充到 80% 停止） |
| 🔧 **配置编辑器** | 在浏览器中编辑所有设置 |
| 📦 **KernelSU 原生** | 包含标准 `module.prop`、`service.sh`、`uninstall.sh` |
| 🤖 **CI/CD** | GitHub Actions：自动构建 ZIP 并自动发布 |

---

## 安装说明

### 环境要求
- 已安装 **KernelSU** 的 Android 设备
- 设备上可用 Python 3（或通过 Termux）

### 安装步骤
1. 从 [Releases](https://github.com/ce11kjw/ChargeControl/releases) 下载最新 ZIP
2. 打开 **KernelSU 管理器** → **模块** → **从存储安装**
3. 选择 `ChargeControl_vX.X.X.zip`
4. 重启设备

### 访问 Web 界面
设备启动后，打开浏览器访问：
```
http://127.0.0.1:8080
```

---

## 充电模式

| 模式 | 电流 | 电压 | 使用场景 |
|------|------|------|----------|
| **普通** | 2000 mA | 4350 mV | 日常使用 |
| **快充** | 4000 mA | 4400 mV | 快速充电 |
| **涓流** | 500 mA | 4200 mV | 过夜充电，延长电池寿命 |
| **省电** | 1000 mA | 4300 mV | 均衡模式 |
| **超级省电** | 300 mA | 4100 mV | 最大程度保护电池健康 |

---

## 从源码构建

> **为什么必须先 `make`？**  
> `service.sh` 在开机时会执行模块目录下的 `charge_control` 二进制文件。  
> 该文件由 C 源码编译而来，**不在 Git 仓库中**（属于构建产物），因此打包前必须先编译。

### 环境要求（构建机/Linux/macOS）

| 工具 | 说明 |
|------|------|
| `gcc` 或交叉编译器 | 编译 C 源码 |
| `make` | 驱动编译流程 |
| `libsqlite3-dev` | SQLite 头文件与静态库 |
| `zip` | 打包 ZIP |

Ubuntu / Debian 一键安装：
```bash
sudo apt-get install gcc make libsqlite3-dev zip
```

### 构建步骤

```bash
# 1. 克隆仓库
git clone https://github.com/ce11kjw/ChargeControl.git
cd ChargeControl

# 2. 编译 C 后端（生成 ./charge_control）
make

# 3. 打包模块 ZIP（build.sh 会自动调用 make，并验证产物）
bash build.sh

# 输出: out/ChargeControl_vX.X.X.zip
```

或一步完成（build.sh 内部会执行 make）：
```bash
bash build.sh
# 输出: out/ChargeControl_vX.X.X.zip
```

### 验证产物

```bash
unzip -l out/ChargeControl_*.zip | grep charge_control
# 应输出：  xxxxxxx  charge_control
```

如果列表中没有 `charge_control`，请先检查 `make` 是否成功，再重新运行 `bash build.sh`。

### 交叉编译（Android ARM64）

```bash
# 使用 NDK 或 Android 工具链中的编译器（只需指定 CC 即可）
make CC=aarch64-linux-android-gcc
bash build.sh
```

---

## API 接口

服务器在 8080 端口提供 REST API。完整参考请查看 [`docs/API.md`](docs/API.md)。

快速示例：
```bash
# 获取电池状态
curl http://127.0.0.1:8080/api/status

# 设置充电上限为 80%
curl -X POST http://127.0.0.1:8080/api/charging/limit \
     -H 'Content-Type: application/json' \
     -d '{"limit": 80}'

# 切换为涓流模式
curl -X POST http://127.0.0.1:8080/api/charging/mode \
     -H 'Content-Type: application/json' \
     -d '{"mode": "trickle"}'
```

---

## 配置

编辑 `config.json` 或使用 Web 界面中的**设置**标签页。  
完整配置格式请查看 [`docs/CONFIG.md`](docs/CONFIG.md)。

---

## 故障排查

**服务器无法启动？**
```bash
adb shell su -c "cat /data/adb/modules/ChargeControl/module.log"
```

**检查运行中的进程：**
```bash
adb shell su -c "ps | grep python"
```

**重启服务器：**
```bash
adb shell su -c "kill \$(cat /data/adb/modules/ChargeControl/server.pid)"
adb shell su -c "cd /data/adb/modules/ChargeControl && nohup python3 server.py &"
```

---

## 更新日志

请查看 [`docs/CHANGELOG.md`](docs/CHANGELOG.md)。

---

## 许可证

MIT 许可证 – 详情请查看 [LICENSE](LICENSE)。

