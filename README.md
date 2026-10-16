# ChargeControl

小米 K60 Ultra（corot / 天玑 9200+）电池充电控制 —— **Magisk 模块 + Android App**。

> 一个 monorepo，两个产物：**root 模块**（模块侧控制 + WebUI）和 **配套 App**（图表与监控）。

---

## 仓库结构

```
ChargeControl/
├── src/battd.c           模块核心（C，单文件，约 2950 行）
├── module/               模块打包内容（Magisk/KSU/APatch 通用）
│   ├── module.prop       模块元信息（version / versionCode / updateJson）
│   ├── customize.sh      安装脚本
│   ├── service.sh        开机启动
│   ├── supervisor.sh     守护进程（15s 巡检，崩溃自动拉起）
│   ├── action.sh         模块管理器按钮
│   ├── webroot/          WebUI（浏览器可访问）
│   └── bin/battd         编译产物（CI 生成）
├── app/                  Android App（Kotlin + Compose）
│   ├── app/src/main/java/com/ce11kjw/chargecontrol/
│   └── ...
├── docs/API.md           模块 HTTP API 文档（233 字段 + 10 端点）
├── Makefile              本地交叉编译（仅开发用）
├── release.sh            发版脚本（版本自增 + tag + push）
└── .github/workflows/
    ├── build-module.yml  模块 CI（aarch64 交叉编译 → Release）
    └── build-app.yml     App CI（Gradle → APK artifact）
```

---

## 模块接 App 的方式

**本地 HTTP + JSON**（KernelSU/APatch 生态的事实标准）：

```
battd :8800  ← WebUI（模块管理器内置的 WebView）
             ← App（ChargeControl-App）
             ← 浏览器（curl 调试）
```

一个服务同时喂三个客户端。协议详见 [`docs/API.md`](docs/API.md)。

**能力协商**：`/api/status` 首部返回 `api_version` 与 `features`，
App 据此判断功能可用性，避免版本错配静默失败。

---

## 编译

**全部在 GitHub Actions 完成**（本地只跑测试）：

| 产物 | 触发条件 | 工作流 |
|---|---|---|
| 模块 zip | 改 `src/**` `module/**` 等 / 打 `v*` tag | `build-module.yml` |
| App APK | 改 `app/**` | `build-app.yml` |

**发版**：
```bash
./release.sh "本次更新说明"
# → 版本自增 → 同步 update.json/CHANGELOG → commit → tag → push
# → CI 编译 → 自动创建 Release
```

---

## 主要功能

### 电池控制
- 充电上限（20-100%）、温度上限（电池 / 充电 IC 双阈值）
- 停止后回充的滞后量（防反复启停）
- 夜间模式、快捷场景、高温 WebHook 告警

### 高级控制（直接操作 MTK 内核节点，默认全关）
真旁路（电源直供）/ 恒压点调整 / 充电电流限流 / 系统原生夜间充电

### 监控与统计
充电曲线（CC/CV 阶段识别）、电池校准、健康度追踪（SoH / 循环 / 内阻）、
CPU 逐核频率、GPU 四平台探测、内存细分、设备详情 60 字段

---

## 兼容性

| 项 | 要求 |
|---|---|
| 设备 | 小米 K60 Ultra（corot），其他 MTK 机型部分可用 |
| Root | Magisk / KernelSU / APatch |
| Android | 16（实测），理论 10+ |
| 架构 | aarch64 |

---

## 许可

见 [LICENSE](LICENSE)。
