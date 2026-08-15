# ChargeControl 电池管家

充电限制 · 温度保护 · 硬件监控 · 电池健康 —— KernelSU / APatch / Magisk 通用模块

## 功能特性

| 类别 | 功能 |
|------|------|
| 🔋 充电控制 | 充电上限（滞回区防抖）、温度保护（超限停充自动恢复）、手动充满一次（可取消） |
| 🔥 硬件监控 | 电量 / 温度 / 电压 / 电流 / 功率 / 充电协议 / SoC / GPU / 充电IC / 机身温度 |
| 📊 电池健康 | SOH 健康度、满充容量、循环次数、剩余循环寿命估算、低健康度换电池提醒 |
| 📜 日志系统 | 事件日志（256KB 循环）、历史趋势（60s 记录、512KB 截断）、一键导出 |
| ⚡ 充电协议 | 自动识别 USB-PD / QC / DCP 及功率（联发科/高通通用） |
| 🎨 WebUI | Tab 导航三页（仪表盘/设置/日志），Alpine.js + Chart.js，3s 实时刷新 |

## 安装

1. 从 [Releases](https://github.com/ce11kjw/ChargeControl/releases) 下载 `ChargeControl.zip`
2. KernelSU / APatch / Magisk Manager → 模块 → 安装
3. 重启
4. 进入模块 → WebUI 打开控制面板

## WebUI 页面

- **仪表盘**：状态横幅（充电中/已暂停/温度保护/手动充满）、电量进度条、温度/功率/循环/限制卡片、健康度卡片、温度计 2×2、趋势图
- **设置**：充电限制开关、高级设置折叠（上限/温度/恢复/间隔）、手动充满按钮
- **日志**：运行日志查看、刷新、全量导出

## 数据路径

| 文件 | 说明 |
|------|------|
| `/data/adb/battery-manager/batt.conf` | 配置（上限/温度/恢复/间隔/开关） |
| `/data/adb/battery-manager/history.json` | 历史记录（60s 一次，512KB 截断） |
| `/data/adb/battery-manager/battd.log` | 事件日志（256KB 循环）+ `.old` 轮转 |
| `/data/adb/battery-manager/webroot/` | WebUI 静态文件 |

## API

| 端点 | 方法 | 说明 |
|------|------|------|
| `/api/status` | GET | 全部状态（28 字段：电池/温度/健康/协议/控制） |
| `/api/limit` | POST | 配置修改（`charge_limit/temp_limit/resume_delta/interval/enabled`） |
| `/api/full` | POST | 手动充满（body `cancel=1` 取消） |
| `/api/history` | GET | 历史 JSON 数组（尾 500 条） |
| `/api/log` | GET | 纯日志文本 |
| `/api/export` | GET | 配置+日志+历史合并导出 |

守护进程监听 `127.0.0.1:8800`（仅本机回环）。

## 硬件兼容

- 控制节点：`/sys/class/power_supply/battery/input_suspend`（MTK/高通多数设备支持）
- 协议识别：`usb/real_type` + `power_max` + `pd_type`
- 硬件温度：`thermal_zone*/type` + `temp`（soc_max / gpu1 / mtk-master-charger / X7_therm）
- 电池健康：`charge_full` / `charge_full_design` / `cycle_count`

> 不同设备 thermal 节点名可能不同，若温度显示 `—` 需按设备适配。

## 编译

```bash
# 交叉编译 ARM64 静态二进制
aarch64-linux-gnu-gcc -static -O2 -s -o battd src/battd.c

# 或使用 Makefile
make
make package   # 生成 ChargeControl.zip
```

## 版本历史

| 版本 | 内容 |
|------|------|
| v1.0.0 | 基础：充电限制 / 温度保护 / 监控 / WebUI |
| v1.1.0 | 硬件温度 / 协议识别 / 日志 / 手动充满 / Tab 导航 |
| v1.2.0 | 健康度 / 循环估算 / 功率 / 倒计时 / 色条 / 2×2 |
| v1.2.1 | 字段落地 / fullOnce 取消 / 安全加固 |
| v1.2.2 | 温度保护优先级 / 功率单位修正 / 扫描鲁棒性 |

## License

GPL-3.0
