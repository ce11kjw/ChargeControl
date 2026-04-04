# ChargeControl ⚡

> Advanced KernelSU module for Android battery charging control, featuring a modern Web UI, multi-mode charging, real-time statistics, and automated CI/CD.

[![Build & Test](https://github.com/ce11kjw/ChargeControl/actions/workflows/build.yml/badge.svg)](https://github.com/ce11kjw/ChargeControl/actions/workflows/build.yml)

---

## Features

| Feature | Description |
|---------|-------------|
| 🌐 **Web Dashboard** | Modern responsive UI with dark/light theme |
| ⚡ **Multi-Mode Charging** | Normal, Fast, Trickle, Power Saving, Super Saver |
| 📊 **Statistics** | Daily/weekly/monthly charts, CSV & JSON export |
| 🌡️ **Temp Protection** | Auto-throttle and stop charging when too hot |
| 🎯 **Charge Limit** | Set max charge percentage (e.g. stop at 80%) |
| 🔧 **Config Editor** | Edit all settings from the browser |
| 📦 **KernelSU Native** | Proper `module.prop`, `service.sh`, `uninstall.sh` |
| 🤖 **CI/CD** | GitHub Actions: auto-build ZIP + auto-release |

---

## Installation

### Requirements
- Android device with **KernelSU** installed
- Python 3 available on the device (or via Termux)

### Steps
1. Download the latest ZIP from [Releases](https://github.com/ce11kjw/ChargeControl/releases)
2. Open **KernelSU Manager** → **Modules** → **Install from storage**
3. Select `ChargeControl_vX.X.X.zip`
4. Reboot the device

### Access the Web UI
After the device boots, open a browser and navigate to:
```
http://<device-ip>:8080
```

---

## Charging Modes

| Mode | Current | Voltage | Use Case |
|------|---------|---------|----------|
| **Normal** | 2000 mA | 4350 mV | Everyday use |
| **Fast** | 4000 mA | 4400 mV | Quick top-up |
| **Trickle** | 500 mA | 4200 mV | Overnight, battery longevity |
| **Power Saving** | 1000 mA | 4300 mV | Balanced |
| **Super Saver** | 300 mA | 4100 mV | Maximum battery health |

---

## Building from Source

```bash
git clone https://github.com/ce11kjw/ChargeControl.git
cd ChargeControl
bash build.sh
# Output: out/ChargeControl_vX.X.X.zip
```

---

## API

The server exposes a REST API on port 8080. See [`docs/API.md`](docs/API.md) for the full reference.

Quick examples:
```bash
# Get battery status
curl http://localhost:8080/api/status

# Set charge limit to 80%
curl -X POST http://localhost:8080/api/charging/limit \
     -H 'Content-Type: application/json' \
     -d '{"limit": 80}'

# Switch to trickle mode
curl -X POST http://localhost:8080/api/charging/mode \
     -H 'Content-Type: application/json' \
     -d '{"mode": "trickle"}'
```

---

## Configuration

Edit `config.json` or use the **Settings** tab in the Web UI.  
See [`docs/CONFIG.md`](docs/CONFIG.md) for the full schema.

---

## Troubleshooting

**Server not starting?**
```bash
adb shell su -c "cat /data/adb/modules/ChargeControl/module.log"
```

**Check running processes:**
```bash
adb shell su -c "ps | grep python"
```

**Restart the server:**
```bash
adb shell su -c "kill \$(cat /data/adb/modules/ChargeControl/server.pid)"
adb shell su -c "cd /data/adb/modules/ChargeControl && nohup python3 server.py &"
```

---

## Changelog

See [`docs/CHANGELOG.md`](docs/CHANGELOG.md).

---

## License

MIT License – see [LICENSE](LICENSE) for details.

