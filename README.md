# ChargeControl 电池管家

充电限制 · 温度保护 · 硬件监控 · 电池健康 —— KernelSU / APatch / Magisk 通用模块

## 功能特性

| 类别 | 功能 |
|------|------|
| 🔋 充电控制 | 充电上限（滞回区防抖）、温度保护（超限停充自动恢复）、手动充满一次（可取消）、手动暂停/恢复 |
| 🔥 硬件监控 | 电量 / 温度 / 电压 / 电流 / 实时功率（双向充放） / 充电协议 / SoC / GPU / 充电IC / 机身温度 |
| 📊 电池健康 | SOH 健康度、满充容量、设计容量、循环次数、剩余循环寿命估算（至60%）、低健康度换电池提醒 |
| 📜 日志系统 | 事件日志（256KB 循环）、历史趋势（60s 记录、512KB 截断）、一键导出、历史记录开关 |
| ⚡ 充电协议 | 自动识别 USB-PD / QC / DCP 及功率（联发科/高通通用），2×2 卡片布局 |
| 🎨 WebUI | 三页 Tab（仪表盘/设置/日志），Alpine.js + Chart.js 3s 实时刷新，渐变趋势图 |
| 🛡️ 旁路充电 | 自适应探测（支持设备显示开关，不支持显示"不支持"） |
| 🧩 多设备适配 | thermal 传感器多品牌匹配（soc_max/tsens/cpu-therm、gpu1/gpu-therm、charger/bq/smb、X7/skin/case） |
| 📱 守护进程 | 安装时全节点检测，WebUI 显示进程名+PID+CPU占用率 |

## 安装

1. 从 [Releases](https://github.com/ce11kjw/ChargeControl/releases) 下载 `ChargeControl.zip`
2. KernelSU / APatch / Magisk Manager → 模块 → 安装
3. 重启
4. 进入模块 → WebUI 打开控制面板

## WebUI

### 仪表盘
- **状态横幅**：充电中/已达上限/温度保护/手动充满/已暂停/限制关闭
- **电量大数字**：带 1 位小数（如 77.1%），进度条同步
- **4 卡片**：电池温度、实时功率（充放颜色）、守护进程（PID+CPU%）、循环次数
- **健康度大模块**：健康%/评级/满充容量/设计容量/剩余循环/换电池提醒
- **温度 2×2**：SoC / GPU / 充电IC / 机身，带颜色圆点
- **协议 2×2**：充电协议/协商功率/PD版本/连接状态
- **电压/电流/电量/旁路 2×2**：电压(3位小数)/电流/小数电量/旁路状态
- **趋势图**：双轴渐变填充，电量+温度，hover tooltip

### 设置
- 充电限制开关
- 4 个滑块（充电上限/温度上限/恢复差值/轮询间隔），带二次确认
- 手动充满（30分钟超时可取消）
- 手动暂停/恢复（温度保护时自动禁用）
- 历史记录开关
- 旁路充电按钮（自适应，不支持时按钮禁用显示"不支持"）

### 日志
- 运行日志查看（10s 自动刷新）
- 全量导出（配置+日志+历史）

## 节点检测（安装时）

安装时自动扫描并输出所有关键节点状态：

```
- KernelSU 环境
- 电池控制节点
  [Y] input_suspend = 1 (可控制充电)
  [Y] charge_control_limit = 5 (备选节点)
- 电池监控节点
  [Y] capacity = 83
  [Y] temp = 315
  ...
- 安装完成，请重启
```

## 数据路径

| 文件 | 说明 |
|------|------|
| `/data/adb/battery-manager/batt.conf` | 配置（上限/温度/恢复/间隔/开关/历史开关） |
| `/data/adb/battery-manager/history.json` | 历史记录（60s 一次，512KB 截断） |
| `/data/adb/battery-manager/battd.log` | 事件日志（256KB 循环）+ `.old` 轮转 |
| `/data/adb/battery-manager/webroot/` | WebUI 静态文件 |

## API

| 端点 | 方法 | 说明 |
|------|------|------|
| `/api/status` | GET | 全部状态（36 字段：电池/温度/健康/协议/控制/进程/旁路） |
| `/api/limit` | POST | 配置修改（`charge_limit/temp_limit/resume_delta/interval/enabled/history_enabled/bypass`） |
| `/api/full` | POST | 手动充满（body `cancel=1` 取消） |
| `/api/pause` | POST | 手动暂停/恢复（`pause=1` 暂停，`pause=0` 恢复） |
| `/api/history` | GET | 历史 JSON 数组（尾 500 条） |
| `/api/log` | GET | 纯日志文本 |
| `/api/export` | GET | 配置+日志+历史合并导出 |

守护进程监听 `127.0.0.1:8800`（仅本机回环）。

## 硬件兼容

- **控制节点**：`/sys/class/power_supply/battery/input_suspend` 或 `usb/input_suspend`（MTK/高通多数设备支持）
- **协议识别**：`usb/real_type` + `power_max` + `pd_type`
- **硬件温度**：`thermal_zone*/type` + `temp`（多品牌自动匹配）
- **电池健康**：`charge_full` / `charge_full_design` / `cycle_count`
- **高精度电量**：`bms/capacity_raw`（x100 精度，如 7714=77.14%）
- **旁路充电**：自动探测 `/sys/class/power_supply/*/bypass_charger` 或 `charge_bypass`

## 编译

```bash
# 交叉编译 ARM64 静态二进制
aarch64-linux-gnu-gcc -static -O2 -s -o battd src/battd.c

# 或使用 Makefile（推荐）
make          # 编译
make package  # 编译 + 打包为 ChargeControl.zip
```

## 版本历史

| 版本 | 内容 |
|------|------|
| v1.0.0 | 基础：充电限制 / 温度保护 / 监控 / WebUI |
| v1.1.0 | 硬件温度 / 协议识别 / 日志 / 手动充满 / Tab 导航 |
| v1.2.0 | 健康度 / 循环估算 / 功率 / 倒计时 / 色条 / 2×2 |
| v1.2.1 | 字段落地 / fullOnce 取消 / 安全加固 |
| v1.2.2 | 温度保护优先级 / 功率单位修正 / 扫描鲁棒性 / 实时功率双向 / 小数电量 / 手动暂停标志 / 历史开关 / 图表渐变 / 守护进程卡片 / 多设备 thermal 适配 / 旁路探测 / 高级设置滑块+二次确认 / paused 温度保护区分 / write_str 失败日志 / 旁路读路径统一 |

## License

GPL-3.0
