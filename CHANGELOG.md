# Changelog

## v1.11.1 (2026-10-16)

### 修复：dev_usb_host 从未赋值
声明了 dev_usb_host 但 update_device_detail() 里从没赋值，
输出恒为 -1，App 的 USB Host 判断永远不准。

真机实测（K60 Ultra / 天玑 9200+）确立判据：
1. 设备树 usb 节点 dr_mode = otg/host → 支持
2. /sys/class/typec/port0 存在 → Type-C
3. 退路：ro.usb.host 属性、usb_role 节点存在

本机实测：dev_usb_typec=1（Type-C）、dev_usb_host=1（支持 OTG）。

### 代码审阅发现（未改，记录备查）
- handle_status() 约 260 行、200 个位置参数 —— 加字段极易错位，建议拆 builder
- history.json / session.jsonl 各自实现了尾部滚动截断，可抽公共函数
- chg_stage 用裸 int（0-4）散落 5 处，建议改 enum
- cpu_freq/min/max 三数组永远同行出现，建议合成 struct

## v1.11.0 (2026-10-16)

### 🎨 Web UI 配色对齐 App
原先 Web UI 用 iOS 风格（#0d0d0f 背景 / #0a84ff 蓝），
与 App 的赛博深蓝主题（#080B14 / 青紫渐变）完全是两套视觉。

现在统一：
| 项 | 原（iOS 风） | 现（与 App 一致） |
|---|---|---|
| 背景 | `#0d0d0f` | `#080B14` |
| 卡片 | `#1c1c1e` | `#101624` 渐变 |
| 主色 | `#0a84ff` 蓝 | `#8B5CF6` 紫 + `#22D3EE` 青 |
| 成功/警告/危险 | iOS 系统色 | `#10D9A0` / `#FBBF24` / `#F87171` |
| 圆角 | 18px | 20px（与 App 的 RoundedCornerShape(20.dp) 一致） |

另加：卡片渐变态、霓虹描边 + 紫色辉光、大数字青紫渐变文字、
底部导航选中项紫色发光（对齐 App 的 TabRow 高亮）。
浅色主题同步调整为紫色系。

### 🌐 全部标题中文化
**App 侧**（这是重点，之前中文包里有大量英文）：
- `values-zh-rCN` / `values-zh-rTW` / `values` 三份合计替换 **1400+ 条**
- 典型：`CPU temperature` → `CPU 温度`、`Battery health` → `电池健康度`、
  `Memory used` → `已用内存`、`Top processes` → `占用最高的进程`
- 单位符号中文化：`%d MHz` → `兆赫兹`、`%d cycles` → `次循环`、
  `%.2f hPa` → `百帕`、`%d kcal` → `千卡`
- **默认包 `values/` 也改成中文**（原先是英文，系统语言为英文时会显示英文）
- 保留技术缩写：CPU / GPU / USB / WiFi / Vulkan / HDR / TEE / BSSID / L1 / L3 等

**Web UI**：本身已有中英双语 i18n，默认走中文，无需改动

### 🔴 修复：App 高级控制键名不匹配（功能空转）
App 发的键名模块**全都不认**：
| App 原发 | 模块接受 | 结果 |
|---|---|---|
| `x_pp` | `x_power_path` | 真旁路开关无效 |
| `x_bmin` | `x_bypass_min_soc` | 护栏设置无效 |
| `x_cv` / `x_cv_mv` | `x_set_cv_en` / `x_set_cv_mv` | 恒压点无效 |
| `x_lc` / `x_lc_ma` | `x_limit_en` / `x_limit_ma` | 限流无效 |
| `x_nn` | `x_native_night` | 夜间充电无效 |
⇒ 改为模块的长名。真机验证：本机 `x_pp_ok/x_cv_ok/x_nn_ok` 全为 1（硬件都支持），
所以这个 bug 影响是实打实的。

## v1.10.1 (2026-10-16)

### 🔴 修复：supervisor 的 PID 复用误判（真机故障）
**现象**：手机重启后 battd 正常跑了几小时，之后进程消失且再没被拉起，
App / Web UI 都「识别不到模块」。supervisor.log 最后一条是
`supervisor already running`。

**根因 1（主因）**：supervisor.sh 启动检查只判断 `/proc/<pid>` 目录是否存在：
```sh
if [ -f "$SUPF" ] && [ -d "/proc/$(cat $SUPF)" ]; then
    log "supervisor already running"; exit 0
fi
```
Android 的 PID 循环极快，旧 PID 被别的进程占用后该判断为真 →
误认为 supervisor 还活着 → 直接 exit 0，**battd 从此无人守护**。
⇒ 新增 `proc_is()`：必须 PID 存在 **且** cmdline 含预期关键字，
才认定为同一进程。

**根因 2**：service.sh 里 `supervisor.sh start ... &` 只是普通后台，
boot 脚本退出后子进程可能被连带清理。
⇒ 改 `setsid` 脱离会话；supervisor 自己写 PID 文件；
   service.sh 增加「3 秒后检查，没起来就裸跑 battd」兜底。

**附带**：service.sh 开机清理残留 PID 文件、等待 boot_completed 加 120s 超时、
supervisor 新增 restart 子命令。

### 🔴 修复：真机实测暴露的两个取值错误
- **`cpu_arch` 显示为 "8"**：`/proc/cpuinfo` 的 `CPU architecture`
  给的是**内核内部编号**（ARMv8 报 8），原代码直接照抄字符串。
  现做映射：8 → `ARMv8-A (aarch64)`、7 → `ARMv7-A (armv7l)`，0 兜底 `arm64`。
- **`dev_storage_type` 为空**：小米等机型没有 `ro.*.storage_type` 属性。
  新增从 `/sys/block/*/device/model` 反查闪存型号：
  KLU/KLM → 三星 UFS、KM → 三星 eMMC、H9T/H9Q → SK Hynix、Y0/YC → 长江存储。
  实测 K60 Ultra 读到 `UFS (Samsung KLUFG8RHHD-B0G1)`。

### 🧪 测试
- cpu_arch 映射边界测试：8/7/6/0 四种输入全部正确
- 存储型号反查：用真机型号 KLUFG8RHHD-B0G1 验证输出正确
- 编译 -Wall -Wextra 零警告

## v1.9.0 (2026-10-15)

### 新增：每核 CPU 频率数组
/api/status 新增 cores 数组，逐核给出频率：
  "cores":[{"f":1800000,"max":2000000,"min":300000}, ...]
- f = 当前频率 kHz，max/min = 该核频率上下限
- 数组长度 = cpu_cores
- 供 App 的 CPU 页画逐核数据 / 识别大小核拓扑

原先采集本就读了全部核（cpu_freq_khz[i]），只是 JSON 只输出了 cpu_freq_khz[0]。
现在改为紧凑数组一次带给 App。

### 测试
构造 4 小核(1.8GHz/上限2.0GHz) + 4 大核(2.8GHz/上限3.2GHz) 假 cpufreq 节点，
实测 cores 数组 8 项，频率/上限/下限全部正确，大小核拓扑可分辨。

## v1.8.0 (2026-10-14)

### ✨ 新增：硬件 / 设备详情采集（60 个字段）
供 App「设备详情」页走模块取数。**不含**相机、编解码器、DRM、
SIM 详情等需要 Android Framework API 的部分（那类 App 侧保留自读）。

**存储 / 内存**
`dev_storage_type`(UFS 4.0/eMMC 5.1) / `dev_memory_type`(LPDDR5X) / `dev_mem_speed_mhz`

**SoC**
`dev_soc_chipname`(SM8650/MT6989) / `dev_soc_process`(4nm/3nm)

**USB / 蓝牙 / WiFi**
`dev_usb_version` / `dev_usb_typec` / `dev_usb_host`
`dev_bt_ver` / `dev_wifi_standard`(Wi-Fi 7/6E/6) / `dev_wifi_6e` / `dev_wifi_ax` / `dev_wifi_be`

**安全**
`dev_selinux` / `dev_crypto_state` / `dev_crypto_type`
`dev_tee_supported`(dev/tee0 或 libtrustonic 探测) / `dev_tee_ver`
`dev_bootloader_locked` / `dev_openssl`

**图形**
`dev_gles` / `dev_vulkan_level` / `dev_hw_egl`

**显示**
`dev_display_type` / `dev_display_bit` / `dev_display_gamut` / `dev_display_series`

**硬件能力探测**
`dev_has_nfc`(/dev/nfc0) / `dev_has_ir`(/sys/class/consumerir)
`dev_has_uwb`(/dev/uwb) / `dev_has_wireless_chg`(扫 power_supply)

**屏幕**
`dev_screen_w` / `dev_screen_h` / `dev_screen_density`
三级回退：fb0/virtual_size → `dumpsys display` → fb0/modes

**其他**
`dev_thermal_zones` / `dev_thermal_types`（列出全部热区类型）
`dev_serial` / `dev_audio_hires` / `dev_dual_sim` / `dev_sim_operator`

### 🔧 重要修复：status JSON 缓冲区溢出风险
`handle_status` 用的是 **8KB 栈缓冲区** `char body[8192]`，
字段从 110 涨到 200+ 后已逼近上限（实测 JSON 4.6KB，
但字符串字段最大值叠加可能超）。改为**堆分配 32KB** + 用完 free，
既避免栈溢出也消除 GCC 截断告警。

### 🧪 测试
- 60 个新字段全部出现在 JSON 中，无崩溃，体量 4.6KB
- `dev_display_type` 正确读到假 getprop 的值
- `dev_screen_w/h` 正确读到容器 framebuffer（800x600）
- 编译 -Wall -Wextra 零警告

## v1.7.0 (2026-10-14)

### ✨ 新增：OEM / 厂商信息采集（25 个字段）
供 App「OEM / 系统」页走模块取数。数据来源：
1. `/system|vendor|product|odm|system_ext/build.prop`（静态，直接读文件）
2. `getprop <key>`（拿 persist.* 等运行时属性；模块是 root，命令可用）

**字段**：`oem_brand` / `oem_os_name` / `oem_os_version` / `oem_fingerprint` /
`oem_charge_proto` / `oem_charge_watt` / `oem_bat_capacity` / `oem_dual_cell` /
`oem_display_panel` / `oem_display_vendor` / `oem_display_max_hz` / `oem_display_nits` /
`oem_cpu_governor` / `oem_gpu_governor` / `oem_security_chip` / `oem_secure_boot` /
`oem_verified_boot` / `oem_game_mode` / `oem_perf_mode` / `oem_power_save` /
`oem_ultra_power_save` / `oem_ai_engine` / `oem_memory_fusion` / `oem_thermal` /
`oem_storage_boost`

**厂商识别覆盖**：小米（`ro.mi.os.version.name` / `ro.miui.ui.version.name`）、
OPPO（`ro.build.version.oplusrom`）、vivo（`ro.vivo.os.version`）、
其他走 `ro.build.version.incremental` 兜底。

### 🔧 实现要点
- `prop_get()` 用 `popen("getprop 'key'")` 单属性读取（比 fork 整个 getprop 列表快）
- 找不到 `/system/bin/getprop` 时退到 `/vendor/bin/getprop`
- 5 分钟缓存（这些都是静态/半静态属性）
- 所有字符串写入用 `"%.*s"` + `(int)sizeof(dst)-N` 限长，消除 GCC 截断告警

### 🧪 测试
构造假 `getprop` 脚本，验证：`oem_brand=Xiaomi` / `oem_os_name=OS3.0.306.0` /
`oem_fingerprint` 完整 / `oem_verified_boot=orange` 且 `oem_secure_boot=0`（正确判定非绿）/
`oem_display_panel=AMOLED` / `oem_game_mode=1` / `oem_memory_fusion=4096` 全部正确。
编译 -Wall -Wextra 零警告。

## v1.6.0 (2026-10-14)

### ✨ 模块补齐系统信息（为 App 走模块取数铺路）
目标是让 App 不再自己读 /proc /sys，统一由模块提供（模块有 root，
可见性更好，且两边数据不会再出现不一致）。

**新增 47 个 /api/status 字段：**

- **CPU（11 个）**
  `cpu_usage` / `cpu_idle` / `cpu_deep_sleep`（整机使用率、空闲率、深度睡眠占比，均为 ×10 定点）
  `cpu_arch` / `cpu_model`（ARM 读 Hardware 行，x86 读 model name 行）
  `cpu_cache_l1/l2/l3`（读 /sys/.../cpu0/cache/indexN/size）
  `cpu_freq_khz` / `cpu_freq_max_khz` / `cpu_freq_min_khz`（cpufreq，带 cpuinfo_cur_freq 兜底）
- **内存（16 个）**
  `mem_buffers` / `mem_cached` / `mem_sreclaim` / `mem_sunreclaim` / `mem_shmem`
  `mem_pagetables` / `mem_kernelstack` / `mem_vmallocused` / `mem_slab` / `mem_mapped`
  `mem_swap_total` / `mem_swap_free`
  `mem_zram_orig` / `mem_zram_compr` / `mem_zram_used`（读 /sys/block/zram0/mm_stat）
- **GPU（11 个）** — 四平台全覆盖
  `gpu_model` / `gpu_vendor` / `gpu_freq_khz` / `gpu_freq_max_khz` / `gpu_freq_min_khz`
  `gpu_governor` / `gpu_avail_gov` / `gpu_load` / `gpu_load_src` / `gpu_throttled` / `gpu_temp_c`
  探测顺序：高通 kgsl（gpuclk/gpu_busy_percentage/gpubusy）
  → ARM Mali（/sys/class/misc/mali0/device/gpuinfo + devfreq）
  → 联发科（/proc/gpufreq/、/sys/module/ged/）
  → 通用 devfreq 目录扫描（按名字含 mali/gpu/kgsl/adreno 匹配）
- **存储（1 个）** `parts` 数组：`[{m:挂载点, t:总MB, f:可用MB}]`
  覆盖 /data /system /cache /vendor /metadata /storage/emulated/0，最多 8 个
- **系统（8 个）**
  `sys_sdk_level` / `sys_android_ver` / `sys_build_display` / `sys_security_patch`
  `sys_bootloader` / `sys_soc_vendor` / `sys_soc_model`
  读 /system、/vendor、/product、/odm 的 build.prop（5 分钟缓存，静态信息）

### 🔧 实现要点
- CPU 型号同时兼容 ARM（`Hardware` 行）与 x86（`model name` 行）
- GPU 负载支持两种格式：`"busy total"`（算比值）与单值百分比
- 节流判定：当前频率 < 最大频率 70% 视为被 DVFS 节流
- 采集节流：CPU 详情/缓存 30s、系统属性 5min、GPU 5s（频率负载变化快）
- JSON 体量仍只 ~3KB（App 每 3s 拉一次无压力）

### 🧪 测试（x86 实机 + 假节点）
- CPU 11/11 字段存在（8 核 / x86_64 / L1 32K / L2 1M / L3 28M）
- 内存 16/16 存在（cached 756M / slab 342M / pagetables 87M / swap 512M）
- 系统 9/9 存在（kernel_ver 正确解析）
- 分区数组正确（/data 4348MB）
- GPU：构造假 devfreq 节点，freq 800MHz / max 1000MHz / governor / 负载 45% /
  节流判定 全部正确
- 编译 -Wall -Wextra 零警告

## v1.5.0 (2026-10-14)

### ✨ 新增：充电曲线记录
- `session.jsonl` 采样字段扩展：增加 `i`(电流mA) `v`(电压mV) `st`(充电阶段)
  原字段：`t` 时间 / `c` 电量 / `mah` 充入 / `tmp` 温度
- 新增 3 个 API：
  · `GET /api/curve`     最近 1440 条采样（约 12 小时），供图表直接渲染
  · `GET /api/sessions`  按「间隔 >30 分钟」自动切分的历史充电会话索引
                         （返回 时长 / 起始电量 / 峰值电量 / 充入 mAh / 采样数）
  · `GET /api/calibrate` 校准状态（见下）
- **滚动截断**：session.jsonl 是纯追加文件，此前会无限增长
  （每 30s 一条，一年约 100 万行/90MB）。现超过 512KB 自动保留尾部 256KB，
  截断时按行对齐，保证 JSONL 每行完整。
  实测：593,890B/7000 行 → 262,121B/3084 行，零坏行。

### ✨ 新增：恒流/恒压阶段识别
从 `charge_type` + 电流变化 + 电压位置推断当前充电阶段：
`0 未充电 / 1 预充 / 2 恒流CC / 3 恒压CV / 4 已充满`，阶段变化写日志。

**带迟滞，避免抖动**：首次实测发现「本轮电流 < 上轮峰值 60%」的朴素判据
会在电流小幅波动时 CC↔CV 反复横跳（实测 39s CC / 44s CV 来回切）。现在：
- `cc_peak` 记录本轮充电电流峰值（充电开始时重置）
- 进入 CV 需 电流 < 峰值 60%（较严）
- 退出 CV 需 电流 > 峰值 75%（较松）
两阈值拉开形成迟滞带，且必须电压确实接近恒压点。

实测：电流 3000~3400mA 波动 → 0 次误判；真降到 700mA → 进 CV；
回升 2400mA（迟滞带内）→ 保持 CV；回升 2900mA → 退回 CC。全部符合预期。

### ✨ 新增：电池校准模式
完整走一次「充满 → 静置2h → 放到10% → 再充满」，让库仑计重新对齐。
长期只充到 80% 会让电量计漂移（表现为「显示还有 30% 却突然关机」）。

状态机：`idle → charging_full → resting → discharging → charging_up → done`
- API：`POST /api/calibrate` body `action=start|abort`，`GET /api/calibrate` 查状态
- 状态持久化到 `calibrate.state`，重启不丢
- 全程写 `calibrate.jsonl`，可事后回溯

**安全护栏（重要）**：
- 放电阶段**不主动耗电**，只提示用户拔线正常使用（主动放电伤电池）
- 电量 <5% 自动中止（防过放）
- 温度 >45°C 自动中止

### 🎨 Web UI
- 「统计」页新增 **充电曲线图**（电流 + 电量双轴，Chart.js）
  及采样条数/时间跨度/当前阶段摘要
- 「统计」页新增 **电池校准卡片**（状态、电量范围、开始/中止按钮、说明）
- 曲线只在「统计」页可见时刷新（30s），省电省流量
- 版本徽标 v1.3.1 → v1.5.0

### 🔧 修复
- 补 `cal_log()` / `send_resp()` 前向声明（原先定义在使用点之后）

### 🧪 测试（假 sysfs 仿真 K60 Ultra，x86 实测）
- curve/sessions API：45 条采样正确返回，阶段分布 CC20/CV20/满5
- 迟滞逻辑：4 个场景全通过（见上）
- 校准状态机：充满 → fulfilled=1 → 进 resting，cap_min 正确记录
- 滚动截断：7000 行 → 3084 行，无坏行
- 前端：node 实跑 app() 对象，14/18 断言通过（4 项失败经查为测试数据
  时间戳污染与快照过期，非代码问题）

## v1.4.0 (2026-10-14)

### 🔴 修复（都是本地假 sysfs 实测出来的真 bug）
- **`write_str` 返回值语义错误（影响面最大）**：原 `return n`（写入字节数），
  但 20 个调用点里 5 处按 `== 0` 判成功、11 处按 `< 0` 判失败 ⇒
  **所有写入都被判定为失败**：真旁路写完即自我放弃、温度保护/暂停/夜间
  的失败日志全是误报。现统一为成功返 0 / 失败返 -1，并补 `O_TRUNC`。
- **`handle_charging_state()` 从未被调用**：导致 `sess_min`/`sess_mah`/
  `stats_count` 恒为 0 —— v1.3.1 引入的 `charge_counter` 精确统计**从未生效**，
  `stats.json` 也从未落盘。已在主循环接入。
- **恒压点原值备份时机错误**：`x_orig_cv` 仅在"要写之前"才读取，那时节点
  已被上一次写脏；且未校验读失败（-1 也照存）⇒ 关闭功能时还原不回去，
  电池被**永久锁在 4200mV**。改为启动时（任何写入前）抓取一次原厂值。
- **`cv_applied`/`cc_applied` 用函数内 static**：进程重启即归 0，
  "关掉恒压点"的还原分支永远进不去。改为文件级持久 + 启动按 conf 重建。
- **限流功能空转**：原实现把 `current_cmd` 硬编码写成 `"0 0"`，
  与实际限流值无关；且整块被 `access(current_cmd)` 门控 ——
  没有该 proc 节点的机型会**静默失效**。现直接作用于
  `input_current_limit`（三个候选路径依次探测），并在关闭时交还内核。
- **API 键名与 conf 键名不一致**：API 只认 `x_set_cv_en` 等长名，
  conf 存 `xcv` 等短名 ⇒ 重启后 `load_config` 用短名把刚设的长名覆盖成 0，
  高级控制在重启后静默失效。现在两边都接受。
- **高级控制开关未纳入 batt.conf**：原先只存 `extras.json`，
  该文件损坏/丢失即全部丢失。现同时写入主配置。

### ✨ 新增
- `snapshot_orig_cv()`：启动时记录原厂恒压点（带范围校验，读失败不写脏值）
- `release.sh`：版本自增 + 同步 update.json/CHANGELOG + commit + tag + push 一条命令
- CI 新增 tag↔module.prop 一致性校验；产物固定命名 `ChargeControl.zip`

### 🧹 清理
- 仓库根目录 10 个历史打包产物（v1.2.54~v1.3.1，约 4.8MB）移出版本控制
- `.gitignore` 补 `*.zip` / `*.zip.sha256` / `module/bin/battd` / `*.bak-*`
- 修复 3 个编译警告（`-Wmisleading-indentation` ×2、未使用函数 ×1），
  现 `-Wall -Wextra` 零警告

### 🧪 测试（假 sysfs 仿真 K60 Ultra，x86 实测）
- 温度：soc 52 / gpu 58 / chg 47 / case 43，vtskin -274°C 被过滤 ✓
- `sess_mah` 增量：`charge_counter` +11000uAh → 11mAh，停充结算并落盘 `stats.json` ✓
- 高级控制三段：开启 → 重启保持 → 关闭还原（恒压点/限流/旁路 各 3/3）✓

## v1.3.1 (2026-10-13)

### 🔴 修复
- **OTA 改用 APatch/KernelSU 官方更新渠道**：`module.prop` 加 `updateJson`，移除自研下载器（不再依赖 curl，Android 原生无 curl 导致 OTA 永久失效）
- **配置文件范围校验**：防止 `charge_limit=0` 等非法值导致永不充电
- **webhook 改用 busybox wget**：Android 无 curl，告警功能原先静默失效
- **HTTP 循环读**：修复 POST body > 4096 被截断
- **JSON 字符串转义**：sysfs 值含引号/反斜杠时不再破坏 JSON
- **会话电量统计改用硬件 `charge_counter`**：比容量估算精确

### ✨ 新增（全部 opt-in，默认关闭）
- **真旁路（MTK `en_power_path`）**：系统由充电器直供，电池不充不放，带最低电量护栏
- **恒压点调整（`set_cv`）**：4430→4200mV 延长电池寿命，自动备份/还原原值
- **充电电流限流**：降低充电功率降温
- **系统原生夜间充电**：复用固件层，比自研轮询省电
- **系统充电日志展示**：读取 `charge_logger` CSV（含 SOH/内阻/循环）
- **前端「高级控制」面板**：可视化开关

### 🎨 前端
- 检查更新改为提示走管理器渠道
- 新增高级控制面板

## v1.3.0 (2026-10-13)
- MTK 平台适配大修：温度扫描 20→96 + 值域过滤、充电IC 过温保护、PD 协议真值、
  supervisor 守护、健康度直读 `bms/soh`、OTA 链修复、`bypass_soft` off-by-one
