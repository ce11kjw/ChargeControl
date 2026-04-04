# Changelog

All notable changes to ChargeControl are documented here.

---

## [v1.0.1] – 2025-04-04

### Added
- **Modern Web UI** – complete dashboard with dark/light theme, responsive layout (mobile & desktop)
- **Multiple charging modes** – Normal, Fast, Trickle, Power Saving, Super Saver
- **Real-time battery monitoring** – capacity, temperature, voltage, current
- **Charging limit slider** – set maximum charge percentage (0–100%)
- **Temperature protection** – automatic throttle/stop based on configurable thresholds
- **Charging statistics** – daily, weekly, monthly aggregated data with canvas charts
- **Battery health score** – estimated from temperature history
- **CSV and JSON data export**
- **Configuration editor** – edit `config.json` directly from the Web UI
- **Quick presets** – Night / Work / Travel / Save
- **SQLite persistence** – `chargecontrol.db` for sessions and snapshots
- **Background snapshot collector** – samples battery every 30 seconds
- **`charge_control.py`** – core module with sysfs read/write helpers
- **`stats.py`** – statistics & analytics module
- **`server.py`** – full REST API (Flask + CORS)
- **`config.json`** – default configuration file
- **`service.sh`** – robust startup script with Python detection and pip install
- **`uninstall.sh`** – clean module removal
- **`common.prop`** – system property definitions
- **GitHub Actions** – `build.yml` (CI) and `release.yml` (automated releases)
- **`docs/API.md`** – REST API reference
- **`docs/CONFIG.md`** – configuration guide
- **`.gitignore`** – excludes build artefacts and database files

### Changed
- `module.prop` – version bumped to `v1.0.1`, versionCode `101`
- `build.sh` – complete rewrite: validates files, sets permissions, creates ZIP
- `package.sh` – thin wrapper for `build.sh`
- `README.md` – full project documentation

---

## [v1.0.0] – 2025-01-01

### Added
- Initial release
- Basic Flask web server (`server.py`)
- Minimal HTML/CSS/JS frontend
- `module.prop` for KernelSU recognition
