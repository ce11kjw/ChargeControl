"""
ChargeControl - Core Charging Control Module
Manages Android battery charging via kernel sysfs interfaces.
"""

import os
import json
import time
import logging
from datetime import datetime
from pathlib import Path

logger = logging.getLogger(__name__)

# Sysfs paths for battery/charging control
SYSFS_PATHS = {
    "battery_capacity": [
        "/sys/class/power_supply/battery/capacity",
        "/sys/class/power_supply/BAT0/capacity",
    ],
    "battery_status": [
        "/sys/class/power_supply/battery/status",
        "/sys/class/power_supply/BAT0/status",
    ],
    "battery_temp": [
        "/sys/class/power_supply/battery/temp",
        "/sys/class/power_supply/BAT0/temp",
    ],
    "battery_voltage": [
        "/sys/class/power_supply/battery/voltage_now",
        "/sys/class/power_supply/BAT0/voltage_now",
    ],
    "battery_current": [
        "/sys/class/power_supply/battery/current_now",
        "/sys/class/power_supply/BAT0/current_now",
    ],
    "battery_health": [
        "/sys/class/power_supply/battery/health",
        "/sys/class/power_supply/BAT0/health",
    ],
    "charging_enabled": [
        "/sys/class/power_supply/battery/charging_enabled",
        "/sys/kernel/debug/charger/charging_enable",
        "/proc/driver/mmi_battery/charging",
    ],
    "charge_type": [
        "/sys/class/power_supply/battery/charge_type",
        "/sys/class/power_supply/usb/charge_type",
    ],
    "input_current_limit": [
        "/sys/class/power_supply/battery/input_current_limit",
        "/sys/class/power_supply/usb/input_current_limit",
    ],
    "constant_charge_current": [
        "/sys/class/power_supply/battery/constant_charge_current",
        "/sys/class/power_supply/battery/constant_charge_current_max",
    ],
    "charge_control_limit": [
        "/sys/class/power_supply/battery/charge_control_limit",
        "/sys/devices/platform/soc/soc:qti_battery_charger/charge_limit",
    ],
}

CONFIG_PATH = os.path.join(os.path.dirname(__file__), "config.json")

MODES = {
    "normal": {"max_current_ma": 2000, "max_voltage_mv": 4350},
    "fast": {"max_current_ma": 4000, "max_voltage_mv": 4400},
    "trickle": {"max_current_ma": 500, "max_voltage_mv": 4200},
    "power_saving": {"max_current_ma": 1000, "max_voltage_mv": 4300},
    "super_saver": {"max_current_ma": 300, "max_voltage_mv": 4100},
}


def _read_sysfs(key: str) -> str | None:
    """Try each candidate path for a sysfs key and return the first readable value."""
    for path in SYSFS_PATHS.get(key, []):
        try:
            with open(path, "r") as f:
                return f.read().strip()
        except (OSError, PermissionError):
            continue
    return None


def _write_sysfs(key: str, value: str) -> bool:
    """Write a value to the first writable sysfs path for the given key."""
    for path in SYSFS_PATHS.get(key, []):
        try:
            with open(path, "w") as f:
                f.write(str(value))
            logger.info("Wrote %s -> %s", value, path)
            return True
        except (OSError, PermissionError):
            continue
    logger.warning("Could not write '%s' to any path for key '%s'", value, key)
    return False


def load_config() -> dict:
    """Load configuration from config.json."""
    try:
        with open(CONFIG_PATH, "r") as f:
            return json.load(f)
    except (OSError, json.JSONDecodeError) as e:
        logger.error("Failed to load config: %s", e)
        return {}


def save_config(config: dict) -> bool:
    """Save configuration to config.json."""
    try:
        with open(CONFIG_PATH, "w") as f:
            json.dump(config, f, indent=2)
        return True
    except OSError as e:
        logger.error("Failed to save config: %s", e)
        return False


def get_battery_status() -> dict:
    """Read current battery status from sysfs."""
    capacity_raw = _read_sysfs("battery_capacity")
    temp_raw = _read_sysfs("battery_temp")
    voltage_raw = _read_sysfs("battery_voltage")
    current_raw = _read_sysfs("battery_current")

    # Temperature is reported in tenths of degrees Celsius on Android
    temp_c = None
    if temp_raw is not None:
        try:
            val = int(temp_raw)
            temp_c = val / 10.0 if abs(val) > 100 else float(val)
        except ValueError:
            pass

    voltage_mv = None
    if voltage_raw is not None:
        try:
            uv = int(voltage_raw)
            voltage_mv = uv / 1000 if uv > 10000 else uv
        except ValueError:
            pass

    current_ma = None
    if current_raw is not None:
        try:
            ua = int(current_raw)
            current_ma = ua / 1000 if abs(ua) > 10000 else ua
        except ValueError:
            pass

    return {
        "capacity": int(capacity_raw) if capacity_raw is not None else _mock_capacity(),
        "status": _read_sysfs("battery_status") or "Unknown",
        "health": _read_sysfs("battery_health") or "Unknown",
        "temperature": temp_c if temp_c is not None else _mock_temperature(),
        "voltage_mv": voltage_mv,
        "current_ma": current_ma,
        "charging_enabled": _is_charging_enabled(),
        "timestamp": datetime.utcnow().isoformat() + "Z",
    }


def _mock_capacity() -> int:
    """Return a mock capacity value when sysfs is unavailable (dev/test env)."""
    import random
    return random.randint(40, 90)


def _mock_temperature() -> float:
    """Return a mock temperature when sysfs is unavailable."""
    import random
    return round(25.0 + random.uniform(0, 15), 1)


def _is_charging_enabled() -> bool:
    val = _read_sysfs("charging_enabled")
    if val is None:
        return True  # assume enabled when unknown
    return val.strip() not in ("0", "false", "disabled")


def set_charging_enabled(enabled: bool) -> bool:
    """Enable or disable charging."""
    value = "1" if enabled else "0"
    return _write_sysfs("charging_enabled", value)


def set_charge_limit(limit_percent: int) -> bool:
    """Set the maximum charge limit (0-100%)."""
    if not 0 <= limit_percent <= 100:
        raise ValueError(f"Charge limit must be 0-100, got {limit_percent}")
    config = load_config()
    config.setdefault("charging", {})["max_limit"] = limit_percent
    save_config(config)
    return _write_sysfs("charge_control_limit", str(limit_percent))


def set_charging_mode(mode: str) -> bool:
    """Apply a named charging mode (normal/fast/trickle/power_saving/super_saver)."""
    config = load_config()
    mode_cfg = config.get("modes", {}).get(mode) or MODES.get(mode)
    if mode_cfg is None:
        raise ValueError(f"Unknown charging mode: {mode}")

    current_ma = mode_cfg.get("max_current_ma")
    ok = True
    if current_ma:
        ok = ok and _write_sysfs("input_current_limit", str(current_ma * 1000))
        ok = ok and _write_sysfs("constant_charge_current", str(current_ma * 1000))

    config.setdefault("charging", {})["mode"] = mode
    save_config(config)
    return ok


def check_temperature_protection() -> dict:
    """Check temperature and reduce/stop charging if too hot."""
    config = load_config()
    threshold = config.get("charging", {}).get("temperature_threshold", 40)
    critical = config.get("charging", {}).get("temperature_critical", 45)

    status = get_battery_status()
    temp = status.get("temperature", 0)

    action = "none"
    if temp >= critical:
        set_charging_enabled(False)
        action = "charging_stopped"
    elif temp >= threshold:
        set_charging_mode("trickle")
        action = "throttled_to_trickle"
    elif not _is_charging_enabled():
        # Resume if temp has dropped
        set_charging_enabled(True)
        action = "charging_resumed"

    return {
        "temperature": temp,
        "threshold": threshold,
        "critical": critical,
        "action": action,
    }


def get_all_settings() -> dict:
    """Return current configuration merged with live battery data."""
    config = load_config()
    battery = get_battery_status()
    return {
        "config": config,
        "battery": battery,
    }
