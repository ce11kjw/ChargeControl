# Changelog

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
