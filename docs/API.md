# ChargeControl 模块 API

**API 版本**：`/api/status` 的 `api_version` 字段（当前 **1**）

模块 `battd` 监听 `127.0.0.1:8800`（仅回环，无鉴权）。

App 通过 HTTP + JSON 与之通信 —— 这也是 KernelSU/APatch 模块 WebUI 的标准做法，
好处是**同一个服务同时喂 WebUI / App / 浏览器调试**。

---

## 端点总览

| 方法 | 路径 | 说明 |
|---|---|---|
| GET | `/api/status` | 全量状态（235 字段） |
| POST | `/api/limit` | 写配置（`application/x-www-form-urlencoded`） |
| POST | `/api/full` | 触发一次充满 |
| POST | `/api/pause` | 暂停 / 恢复充电 |
| GET | `/api/curve` | 本轮充电曲线采样 |
| GET | `/api/sessions` | 历史充电会话列表 |
| POST | `/api/calibrate` | 电池校准（start / abort） |
| GET | `/api/log` | 运行日志（尾部 64KB） |
| GET | `/api/export` | 导出诊断包 |
| GET | `/api/update-check` | 检查模块更新 |

---

## ⭐ 能力协商（App 必读）

`/api/status` **最前面两个字段**用于版本协商：

```json
{"api_version": 1, "features": "curve,sessions,calibrate,cores,parts,x_control,device_detail,oem,sys_props,mem_detail,gpu,soh,pd,session_stat"}
```

**App 侧正确用法**（不要假设字段一定存在）：

1. `api_version < 期望值` → 提示「模块版本过旧，请升级」
2. 用 `features` 判断功能可用性：`"x_control" in features`
3. 单字段仍用 `optInt(key, 默认值)` 兜底

### 能力项清单（14 项）

| 能力 | 覆盖内容 |
|---|---|
| `curve` | `/api/curve` 充电曲线 |
| `sessions` | `/api/sessions` 历史会话 |
| `calibrate` | `/api/calibrate` 电池校准 |
| `cores` | `status.cores` 每核频率数组 |
| `parts` | `status.parts` 分区数组 |
| `x_control` | 高级控制（真旁路/恒压点/限流/原生夜间） |
| `device_detail` | `dev_*` 硬件详情 |
| `oem` | `oem_*` 厂商字段 |
| `sys_props` | `sys_*` 系统属性 |
| `mem_detail` | `mem_*` 内存细分 |
| `gpu` | `gpu_*` 四平台探测 |
| `soh` | 电池健康度 / 循环次数 |
| `pd` | PD 协议真值 |
| `session_stat` | `sess_*` / `stats_*` 充电统计 |

---

## GET /api/status 字段表

共 **235** 个字段，按前缀分组。

### 电池与充电（93 字段）

| 字段 | 当前值 |
|---|---|
| `alerted` | `0` |
| `api_version` | `1` |
| `bat_capacity_level` | `""` |
| `bat_health` | `"Good"` |
| `bat_manufacturer` | `""` |
| `bat_model_name` | `""` |
| `bat_present` | `1` |
| `bat_technology` | `"Li-ion"` |
| `bypass_hw` | `0` |
| `bypass_ok` | `1` |
| `bypass_on` | `0` |
| `calibrate` | `-1` |
| `capacity` | `78` |
| `capacity_disp` | `7800` |
| `capacity_error_margin` | `-1` |
| `case_temp` | `38` |
| `cc_limit_supported` | `0` |
| `cc_limit_val` | `-1` |
| `charge_counter` | `1245567` |
| `charge_done` | `-1` |
| `charge_full_mah` | `5413` |
| `charge_limit` | `80` |
| `charge_now` | `-1` |
| `charge_term_current` | `-1` |
| `charge_type` | `3` |
| `charger_temp` | `-1` |
| `chg_temp` | `47` |
| `chg_temp_limit` | `60` |
| `cl_tail` | `""` |
| `constant_charge_current` | `-1` |
| `constant_charge_current_max` | `-1` |
| `constant_charge_voltage` | `4430000` |
| `constant_charge_voltage_max` | `-1` |
| `control_state` | `"idle"` |
| `cores` | `[{"f": 0, "max": 0, "min": 0}, {"f": 0, "ma…"` |
| `current` | `2900` |
| `current_avg` | `-1` |
| `cycle_count` | `26` |
| `design_mah` | `5380` |
| `device_model` | `""` |
| `est_cycles_left` | `0` |
| `est_full_min` | `0` |
| `est_full_txt` | `"—"` |
| `features` | `"curve,sessions,calibrate,cores,parts,x_con…"` |
| `full_once` | `0` |
| `full_until` | `0` |
| `health` | `100` |
| `health_degrade` | `0.0` |
| `health_rating` | `"优秀"` |
| `history_enabled` | `1` |
| `input_current_limit` | `-1` |
| `input_voltage_settled` | `-1` |
| `internal_resistance` | `-1` |
| `interval` | `5` |
| `ip_addr` | `"eth0"` |
| `jeita_index` | `-1` |
| `kernel_ver` | `"7.0.0-14-generic (buildd@lcy02-amd64-043) …"` |
| `lang` | `0` |
| `limit_enabled` | `1` |
| `mobile_on` | `0` |
| `native_night` | `0` |
| `night` | `0` |
| `part_count` | `1` |
| `parts` | `[{"m": "/data", "t": 4348, "f": 2175}]` |
| `paused` | `0` |
| `power_max` | `-1` |
| `power_mw` | `-12818` |
| `power_path_en` | `1` |
| `proc_name` | `"battd_test"` |
| `proc_pid` | `2446073` |
| `proto_name` | `"未知"` |
| `resume_delta` | `5` |
| `safety_timer_expired` | `-1` |
| `scene` | `0` |
| `soc_temp` | `-1` |
| `status` | `"Charging"` |
| `storage_free` | `2175` |
| `storage_total` | `4348` |
| `temp` | `38` |
| `temp_ambient` | `-1` |
| `temp_limit` | `45` |
| `temp_max` | `-1` |
| `temp_min` | `-1` |
| `theme` | `0` |
| `time_to_empty` | `-1` |
| `time_to_full` | `-1` |
| `uptime_secs` | `2746785` |
| `usb_online` | `-1` |
| `voltage` | `4420` |
| `voltage_ocv` | `-1` |
| `webhook` | `""` |
| `wifi_on` | `0` |
| `wifi_rssi` | `-128` |

### CPU（14 字段）

| 字段 | 当前值 |
|---|---|
| `cpu_abi` | `""` |
| `cpu_arch` | `"x86_64"` |
| `cpu_cache_l1` | `"32K"` |
| `cpu_cache_l2` | `"1024K"` |
| `cpu_cache_l3` | `"28160K"` |
| `cpu_cores` | `8` |
| `cpu_deep_sleep` | `-1` |
| `cpu_freq_khz` | `0` |
| `cpu_freq_max_khz` | `0` |
| `cpu_freq_min_khz` | `0` |
| `cpu_idle` | `810` |
| `cpu_model` | `"Intel(R) Xeon(R) Gold 6138 CPU @ 2.00GHz"` |
| `cpu_pct` | `0` |
| `cpu_usage` | `189` |

### GPU（12 字段）

| 字段 | 当前值 |
|---|---|
| `gpu_avail_gov` | `""` |
| `gpu_freq_khz` | `-1` |
| `gpu_freq_max_khz` | `-1` |
| `gpu_freq_min_khz` | `-1` |
| `gpu_governor` | `""` |
| `gpu_load` | `-1` |
| `gpu_load_src` | `""` |
| `gpu_model` | `""` |
| `gpu_temp` | `-1` |
| `gpu_temp_c` | `-1` |
| `gpu_throttled` | `0` |
| `gpu_vendor` | `""` |

### 内存（18 字段）

| 字段 | 当前值 |
|---|---|
| `mem_avail` | `1724077` |
| `mem_buffers` | `0` |
| `mem_cached` | `664044` |
| `mem_free` | `704532` |
| `mem_kernelstack` | `29984` |
| `mem_mapped` | `0` |
| `mem_pagetables` | `91584` |
| `mem_shmem` | `156` |
| `mem_slab` | `363705` |
| `mem_sreclaim` | `355637` |
| `mem_sunreclaim` | `8068` |
| `mem_swap_free` | `524216` |
| `mem_swap_total` | `524288` |
| `mem_total` | `2000000` |
| `mem_vmallocused` | `369136` |
| `mem_zram_compr` | `-1` |
| `mem_zram_orig` | `-1` |
| `mem_zram_used` | `-1` |

### 硬件 / 设备详情（41 字段）

| 字段 | 当前值 |
|---|---|
| `dev_audio_hires` | `""` |
| `dev_bootloader_locked` | `-1` |
| `dev_bt_ver` | `""` |
| `dev_crypto_state` | `""` |
| `dev_crypto_type` | `""` |
| `dev_display_bit` | `""` |
| `dev_display_gamut` | `""` |
| `dev_display_series` | `""` |
| `dev_display_type` | `""` |
| `dev_dual_sim` | `-1` |
| `dev_gles` | `""` |
| `dev_has_ir` | `0` |
| `dev_has_nfc` | `0` |
| `dev_has_uwb` | `0` |
| `dev_has_wireless_chg` | `0` |
| `dev_hw_egl` | `""` |
| `dev_mem_speed_mhz` | `-1` |
| `dev_memory_type` | `""` |
| `dev_openssl` | `""` |
| `dev_screen_density` | `-1` |
| `dev_screen_h` | `600` |
| `dev_screen_w` | `800` |
| `dev_selinux` | `""` |
| `dev_serial` | `""` |
| `dev_sim_operator` | `""` |
| `dev_soc_chipname` | `""` |
| `dev_soc_process` | `""` |
| `dev_storage_type` | `""` |
| `dev_tee_supported` | `0` |
| `dev_tee_ver` | `""` |
| `dev_thermal_types` | `""` |
| `dev_thermal_zones` | `0` |
| `dev_usb_host` | `1` |
| `dev_usb_typec` | `-1` |
| `dev_usb_version` | `""` |
| `dev_vulkan_level` | `""` |
| `dev_widevine` | `""` |
| `dev_wifi_6e` | `-1` |
| `dev_wifi_ax` | `-1` |
| `dev_wifi_be` | `-1` |
| `dev_wifi_standard` | `""` |

### 厂商（OEM）（25 字段）

| 字段 | 当前值 |
|---|---|
| `oem_ai_engine` | `""` |
| `oem_bat_capacity` | `""` |
| `oem_brand` | `""` |
| `oem_charge_proto` | `""` |
| `oem_charge_watt` | `""` |
| `oem_cpu_governor` | `""` |
| `oem_display_max_hz` | `""` |
| `oem_display_nits` | `""` |
| `oem_display_panel` | `""` |
| `oem_display_vendor` | `""` |
| `oem_dual_cell` | `0` |
| `oem_fingerprint` | `""` |
| `oem_game_mode` | `""` |
| `oem_gpu_governor` | `""` |
| `oem_memory_fusion` | `""` |
| `oem_os_name` | `""` |
| `oem_os_version` | `""` |
| `oem_perf_mode` | `""` |
| `oem_power_save` | `""` |
| `oem_secure_boot` | `0` |
| `oem_security_chip` | `""` |
| `oem_storage_boost` | `""` |
| `oem_thermal` | `""` |
| `oem_ultra_power_save` | `""` |
| `oem_verified_boot` | `""` |

### 系统属性（7 字段）

| 字段 | 当前值 |
|---|---|
| `sys_android_ver` | `""` |
| `sys_bootloader` | `""` |
| `sys_build_display` | `""` |
| `sys_sdk_level` | `-1` |
| `sys_security_patch` | `""` |
| `sys_soc_model` | `""` |
| `sys_soc_vendor` | `""` |

### 高级控制（10 字段）

| 字段 | 当前值 |
|---|---|
| `x_bmin` | `50` |
| `x_cv` | `0` |
| `x_cv_mv` | `4200` |
| `x_cv_ok` | `1` |
| `x_lc` | `0` |
| `x_lc_ma` | `1000` |
| `x_nn` | `0` |
| `x_nn_ok` | `1` |
| `x_pp` | `0` |
| `x_pp_ok` | `1` |

### PD 协议（6 字段）

| 字段 | 当前值 |
|---|---|
| `pd_pdo_count` | `0` |
| `pd_pdo_max_ma` | `0` |
| `pd_pdo_max_mv` | `0` |
| `pd_state` | `""` |
| `pd_svid` | `""` |
| `pd_type` | `-1` |

### 充电会话统计（6 字段）

| 字段 | 当前值 |
|---|---|
| `sess_active` | `1` |
| `sess_mah` | `0` |
| `sess_min` | `22` |
| `stats_count` | `2` |
| `stats_mah` | `11` |
| `stats_min` | `2` |

### 电池健康度（3 字段）

| 字段 | 当前值 |
|---|---|
| `soh_delta_x10` | `0` |
| `soh_first` | `100` |
| `soh_native` | `100` |

---

## POST /api/limit

`application/x-www-form-urlencoded`。**同一开关同时接受短名与长名**

### 基础配置

| 短名 | 长名 | 范围 | 说明 |
|---|---|---|---|
| `enabled` | `limit_enabled` | 0/1 | 充电限制总开关 |
| `charge_limit` | — | 20-100 | 充电上限 % |
| `temp_limit` | — | 30-60 | 电池温度上限 °C |
| `chg_temp_limit` | — | 40-90 | 充电 IC 温度上限 °C |
| `resume_delta` | — | 1-20 | 降温/降电量后回充的滞后量 |
| `interval` | — | 1-60 | 采样间隔（秒） |
| `history_enabled` | — | 0/1 | 是否记录历史 |
| `night` | — | 0/1 | 夜间模式 |
| `ns` / `ne` | — | 0-23 | 夜间起止小时 |
| `scene` | — | 0-3 | 快捷场景 |
| `webhook` | — | url | 高温告警回调（禁引号/反斜杠） |

### 高级控制（`x_control`）

> ⚠️ 这些直接操作 MTK 内核节点，默认全部关闭。

| 短名 | 长名 | 范围 | 说明 |
|---|---|---|---|
| `xpp` | `x_power_path` | 0/1 | 真旁路（电源直供） |
| `xminsoc` | `x_bypass_min_soc` | 20-80 | 旁路最低电量护栏 % |
| `xcv` | `x_set_cv_en` | 0/1 | 恒压点调整开关 |
| `xcvmv` | `x_set_cv_mv` | 3900-4500 | 恒压点 mV |
| `xcc` | `x_limit_en` | 0/1 | 充电电流限流开关 |
| `xccma` | `x_limit_ma` | 1-6000 | 限流值 mA |
| `xnight` | `x_native_night` | 0/1 | 系统原生夜间充电 |

**行为约定**：
- 越界值**不会报错**（HTTP 仍是 200），而是被**静默忽略**
- ⇒ App 必须**回读 status 验证**是否真的生效（见 `ChargeControlViewModel.write()`）
- 布尔字段统一归一为 0/1（`-5` 会被当 `1`）

---

## 数据文件

| 路径 | 说明 |
|---|---|
| `/data/adb/battery-manager/batt.conf` | 配置持久化 |
| `/data/adb/battery-manager/history.json` | 历史采样（超 512KB 滚动截断到 256KB） |
| `/data/adb/battery-manager/session.jsonl` | 充电曲线采样（同上滚动） |
| `/data/adb/battery-manager/calibrate.state` | 校准进度（原子写） |
| `/data/adb/battery-manager/battd.log` | 运行日志 |
