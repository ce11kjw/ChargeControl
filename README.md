# ChargeControl

小米 K60 Ultra（corot / 天玑 9200+）电池充电控制 —— **Magisk / KernelSU / APatch 模块 + PWA WebUI**。

> 一个模块，一套 WebUI。装完即用，**无需重启手机、无需装 App**。

---

## 特性

### 🔋 充电控制
- **充电上限**：60~100% 可调（步进 5%），到达即停
- **温度保护**：电池温度 / 充电IC 温度双阈值
- **停充自动恢复**：迟滞 + 保持时间，不会在阈值上下反复抖
- **拔插豁免**：拔插后 10 分钟内不受上限限制（用户主动要充）
- **高级控制**：真旁路 / 恒压点调整 / 电流限流 / 系统原生夜间充电

### 📊 监控与分析
- **实时状态**：电量 / 温度（SoC/GPU/充电IC/机身）/ 功率 / 电压 / 电流
- **温度分析视图**：横轴 = 充电进度，Catmull-Rom 平滑，45°C 警戒带，事件标记
- **充电热力分布**：哪个电量段最烫，一格一段
- **SOH 健康趋势**：首记 → 当前衰减曲线 + 循环次数
- **充电预估**：从你自己的历史数据学「每跨 10% 实际耗时」（不用 CC/CV 判据）
- **停充时间线**：今天停了几次、什么原因、各停了多久

### 🔔 提醒中心
全部走 **浏览器通知**（PWA）：
- 到达上限提醒
- 充电异常告警（带曲线快照）
- 自动恢复提醒（同原因 10 分钟去重）
- 到指定电量提醒

> 曾经的 Telegram / Webhook 推送已移除 —— App 有实时曲线和本地通知，多一条链路只是多一个故障点。

### ⚡ 免重启更新
- **装模块**：`customize.sh` → `update.sh` 后台热更新，约 10~20 秒生效
- **UI 改动**：WebUI 是静态文件，改完刷新即生效
- 失败自动回滚，日志在 `/data/adb/battery-manager/hot-update.log`

---

## 安装

### 要求
- Android 12+（实测 K60 Ultra / HyperOS 3.0）
- Magisk 20.4+ / KernelSU / APatch
- 已 root

### 步骤

1. 从 [Releases](https://github.com/ce11kjw/ChargeControl/releases/latest) 下载 `ChargeControl.zip`（约 400 KB）
2. 用模块管理器安装（Magisk App / KernelSU / APatch）
3. 安装界面会显示节点探测结果和热更新进度
4. **无需重启**，约 10~20 秒后生效
5. 打开 WebUI 开始使用

### 打开 WebUI

| 方式 | 说明 |
|---|---|
| 模块管理器里点开 | 最快，内置 WebView |
| 浏览器 `http://127.0.0.1:8800` | 功能完整，可「添加到主屏幕」 |

**PWA 安装**：浏览器打开 → 菜单 → 添加到主屏幕 → 桌面图标全屏运行，无地址栏。

---

## 首次使用建议

1. **设置充电上限**：设置页拖动滑动条，建议 80%（日常）/ 100%（出远门）
2. **确认温度阈值**：默认电池 45°C、充电IC 60°C
3. **让充电数据积累**：充几次电后，「充电预估」和「热力分布」会自动变准
4. **偶尔做一次电池校准**：统计页 → 电池校准（充满→静置→放电到 10%→充满）

---

## 数据存放

所有数据本地存储，**不上传任何服务器**：

```
/data/adb/battery-manager/
├── batt.conf                配置
├── battd                    运行中的二进制
├── history.json             历史记录（60s 一点，滚动 512KB）
├── session.jsonl            充电曲线采样（30s 一点）
├── charge_profile.json      充电预估学习结果
├── events/                  异常事件 + 曲线快照（保留 20 条）
├── hot-update.log           热更新日志
└── webroot/                 WebUI 静态文件
```

---

## HTTP API

WebUI 通过 `http://127.0.0.1:8800` 与 battd 通信，**只绑回环，不对外开放**。

主要端点：

| 端点 | 用途 |
|---|---|
| `GET /api/status` | 全量状态（240+ 字段） |
| `GET /api/history` | 历史记录 |
| `GET /api/curve` | 充电曲线采样 |
| `GET /api/events` | 异常事件列表 |
| `GET /api/event?id=evt-X` | 单条事件（含曲线快照） |
| `GET /api/charge_profile` | 充电学习数据 |
| `GET /api/eta?to=80` | 预估剩余时间 |
| `POST /api/limit` | 修改配置 |
| `POST /api/full` | 手动充满 |
| `POST /api/pause` | 暂停/恢复 |

完整文档见 [docs/API.md](docs/API.md)。

---

## 仓库结构

```
ChargeControl/
├── src/battd.c             模块核心（C，单文件）
├── module/                 模块打包内容
│   ├── module.prop         元信息（version / updateJson）
│   ├── customize.sh        安装脚本（节点探测 + 设权限）
│   ├── update.sh           热更新（装完后台替换+重启，不用重开机）
│   ├── service.sh          开机启动（sha256 同步）
│   ├── supervisor.sh       守护进程（15s 巡检，崩溃自愈）
│   ├── action.sh           模块管理器按钮（状态摘要）
│   ├── webroot/            WebUI（含 PWA: manifest/sw/icons）
│   └── bin/battd           CI 编译产物
├── docs/API.md             API 文档
├── Makefile                本地交叉编译（仅开发用）
├── release.sh              发版脚本
└── .github/workflows/
    └── build-module.yml    CI（x86 冒烟测试 → aarch64 编译 → Release）
```

**没有 App**。v2.0.0 起移除（App 里 42% 是 HDR 夜光 + 特效 + 悬浮窗，
且 WebUI 改 PWA 后已完全可替代）。需要旧版 App 请从 v1.19.4 及更早的 Release 下载。

---

## 开发

### 本地编译（仅开发用，正式构建走 CI）

```bash
# 依赖 aarch64 交叉工具链
sudo apt install gcc-aarch64-linux-gnu
make                    # 产出 src/battd
make package            # 打包成 ChargeControl-vX.Y.Z.zip
```

### 冒烟测试

```bash
bash .github/scripts/smoke-test.sh
```
在假 sysfs 上跑 x86 版本，校验 240+ 字段、端点、越界值拒绝、内存泄漏。

### 发版

```bash
./release.sh minor "更新说明"    # 版本自增 + tag + push
```
CI 会自动跑测试、编译、发 Release。

---

## 已知限制

| 项 | 说明 |
|---|---|
| **充电电流非单调** | MTK 智能充电策略动态调流（50% 时最低 200mA，满电 298mA），所以**不用 CC/CV 判阶段**，改用历史数据学习 |
| **无 `charge_type` 节点** | MTK 未导出，无法读官方充电阶段 |
| **`time_to_full_now` 不可用** | 放电时返回残留静态值 |
| **`current_avg` 无意义** | 恒为 -2400，实际用 `current_now` |
| **双电芯** | 部分机型的电流倍率需手动确认 |

---

## License

个人使用项目。欢迎提 Issue，但这不是一个通用商用产品。

---

## 致谢

- 热更新流程参考 [nyehueh/File Manager](https://www.coolapk.com/) 的 `update.sh` 实现
- WebUI 图表使用 [Chart.js](https://www.chartjs.org/)
- 图标基于 [Font Awesome](https://fontawesome.com/) 与 [Lucide](https://lucide.dev/)
