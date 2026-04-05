# Changelog

All notable changes to ChargeControl are documented here.

---

## [v4.1] – 2026-04-05

### Changed
- Fix launcher.py: install deps from requirements.txt then start server.py

---

## [v4.0] – 2026-04-05

### Changed
- feat: add KernelSU remote update support (update.json + workflow step)

---

## [v3.9] – 2026-04-04

### Changed
- Migrate WebUI to KernelSU native exec() API, remove Flask HTTP dependency
- fix: add launcher.py to REQUIRED and INCLUDE arrays in build.sh

---

## [v3.8] – 2026-04-04

### Changed
- docs: update frontend UI access URL to http://127.0.0.1:8080
- Fix service.sh: retry python detection loop, lock against duplicate starts

---

## [v3.7] – 2026-04-04

### Changed
- Update launcher.py to add --no-cache-dir flag to pip install command
- Update release.yml

---

## [v3.6] – 2026-04-04

### Changed
- Update GitHub Actions to Node.js 24 compatible versions
- 将项目所有内容翻译为简体中文
- Move frontend UI files to webroot/ and update server.py and build.sh

---

## [v3.5] – 2026-04-04

### Changed
- Add launcher.py, requirements.txt and improve service.sh Python detection

---

## [v1.0.3] – 2026-04-04

### Changed
- Add workflow_dispatch to release.yml; remove redundant upload-to-release.yml
- feat: implement advanced ChargeControl features - Web UI, multi-mode charging, statistics, CI/CD
- Add post-fs-data.sh script
- Create package.sh for packaging ChargeControl as KernelSU module

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
