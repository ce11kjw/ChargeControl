# ChargeControl Configuration Guide

The module reads and writes `config.json` located in the module directory
(`/data/adb/modules/ChargeControl/config.json`).

---

## Full Schema

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

## Key Fields

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `charging.max_limit` | int | `80` | Stop charging at this % |
| `charging.min_limit` | int | `20` | Resume charging at this % |
| `charging.mode` | string | `"normal"` | Active charging mode |
| `charging.temperature_threshold` | int | `40` | Throttle to trickle above this °C |
| `charging.temperature_critical` | int | `45` | Stop charging above this °C |
| `server.port` | int | `8080` | Web UI port |
| `stats.retention_days` | int | `90` | Days of history to keep in the DB |

---

## Schedule Rules

Schedule rules allow automatic mode switching based on time of day.

```json
"schedule": {
  "enabled": true,
  "rules": [
    { "start": "22:00", "end": "07:00", "mode": "trickle", "max_limit": 80 },
    { "start": "08:00", "end": "18:00", "mode": "normal",  "max_limit": 90 }
  ]
}
```

> **Note:** Schedule enforcement requires the service to be running.

---

## Quick Presets

| Preset | Mode | Max Limit | Threshold | Critical |
|--------|------|-----------|-----------|----------|
| Night | Trickle | 80% | 38°C | 43°C |
| Work | Normal | 90% | 40°C | 45°C |
| Travel | Fast | 100% | 42°C | 47°C |
| Save | Trickle | 60% | 36°C | 42°C |

Presets can be applied from the **Settings** tab in the Web UI.
