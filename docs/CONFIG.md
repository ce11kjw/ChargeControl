# ChargeControl 配置指南

本模块读写位于模块目录下的 `config.json` 文件
（`/data/adb/modules/ChargeControl/config.json`）。

---

## 完整 Schema

```json
{
  "version": "1.0.1",
  "charging": {
    "max_limit": 80,
    "min_limit": 20,
    "mode": "normal",
    "fast_charge_enabled": true,
    "temperature_threshold": 40,
    "temperature_critical": 45
  },
  "modes": {
    "normal":       { "max_current_ma": 2000, "max_voltage_mv": 4350, "description": "Standard" },
    "fast":         { "max_current_ma": 4000, "max_voltage_mv": 4400, "description": "Fast" },
    "trickle":      { "max_current_ma": 500,  "max_voltage_mv": 4200, "description": "Trickle" },
    "power_saving": { "max_current_ma": 1000, "max_voltage_mv": 4300, "description": "Power Saving" },
    "super_saver":  { "max_current_ma": 300,  "max_voltage_mv": 4100, "description": "Super Saver" }
  },
  "schedule": {
    "enabled": false,
    "rules": []
  },
  "notifications": {
    "enabled": true,
    "charge_complete": true,
    "temperature_warning": true
  },
  "server": {
    "host": "0.0.0.0",
    "port": 8080,
    "debug": false
  },
  "stats": {
    "enabled": true,
    "retention_days": 90
  }
}
```

---

## 关键字段

| 字段 | 类型 | 默认值 | 说明 |
|-------|------|---------|-------------|
| `charging.max_limit` | int | `80` | 达到此百分比时停止充电 |
| `charging.min_limit` | int | `20` | 低于此百分比时恢复充电 |
| `charging.mode` | string | `"normal"` | 当前充电模式 |
| `charging.temperature_threshold` | int | `40` | 超过此温度（°C）时降速至涓流充电 |
| `charging.temperature_critical` | int | `45` | 超过此温度（°C）时停止充电 |
| `server.port` | int | `8080` | Web UI 端口 |
| `stats.retention_days` | int | `90` | 数据库中保留的历史天数 |

---

## 计划规则

计划规则允许根据一天中的时间自动切换充电模式。

```json
"schedule": {
  "enabled": true,
  "rules": [
    { "start": "22:00", "end": "07:00", "mode": "trickle", "max_limit": 80 },
    { "start": "08:00", "end": "18:00", "mode": "normal",  "max_limit": 90 }
  ]
}
```

> **注意：** 计划规则的执行需要服务处于运行状态。

---

## 快速预设

| 预设 | 模式 | 最大限制 | 温度阈值 | 临界温度 |
|--------|------|-----------|-----------|----------|
| Night | Trickle | 80% | 38°C | 43°C |
| Work | Normal | 90% | 40°C | 45°C |
| Travel | Fast | 100% | 42°C | 47°C |
| Save | Trickle | 60% | 36°C | 42°C |

预设可在 Web UI 的**设置**选项卡中应用。
