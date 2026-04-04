"""
ChargeControl - Flask Web Server
Provides the REST API and serves the Web UI.
"""

import json
import logging
import os
import threading
import time

from flask import Flask, jsonify, request, send_from_directory
from flask_cors import CORS

import charge_control as cc
import stats as st

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s %(levelname)s %(name)s: %(message)s",
)
logger = logging.getLogger(__name__)

BASE_DIR = os.path.dirname(os.path.abspath(__file__))

app = Flask(__name__, static_folder=BASE_DIR, static_url_path="")
CORS(app)

# ── Initialise database ──────────────────────────────────────────────────────
st.init_db()

# ── Background snapshot collector ────────────────────────────────────────────
_snapshot_interval = 30  # seconds


def _snapshot_worker():
    while True:
        try:
            battery = cc.get_battery_status()
            config = cc.load_config()
            mode = config.get("charging", {}).get("mode", "normal")
            st.record_snapshot(
                capacity=battery.get("capacity", 0),
                temperature=battery.get("temperature", 0),
                voltage_mv=battery.get("voltage_mv"),
                current_ma=battery.get("current_ma"),
                status=battery.get("status", "Unknown"),
                mode=mode,
            )
        except Exception as exc:
            logger.warning("Snapshot error: %s", exc)
        time.sleep(_snapshot_interval)


_snapshot_thread = threading.Thread(target=_snapshot_worker, daemon=True)
_snapshot_thread.start()


# ── Static pages ─────────────────────────────────────────────────────────────

@app.route("/")
def index():
    return send_from_directory(BASE_DIR, "index.html")


# ── Battery / status ─────────────────────────────────────────────────────────

@app.route("/api/status")
def api_status():
    """Return live battery status."""
    return jsonify(cc.get_battery_status())


@app.route("/api/settings")
def api_settings():
    """Return current configuration + live battery data."""
    return jsonify(cc.get_all_settings())


# ── Charging control ─────────────────────────────────────────────────────────

@app.route("/api/charging/enable", methods=["POST"])
def api_enable_charging():
    data = request.get_json(silent=True) or {}
    enabled = bool(data.get("enabled", True))
    ok = cc.set_charging_enabled(enabled)
    return jsonify({"success": ok, "charging_enabled": enabled})


@app.route("/api/charging/limit", methods=["POST"])
def api_set_limit():
    data = request.get_json(silent=True) or {}
    try:
        limit = int(data["limit"])
    except (KeyError, ValueError):
        return jsonify({"error": "limit must be an integer 0-100"}), 400
    try:
        ok = cc.set_charge_limit(limit)
    except ValueError:
        return jsonify({"error": "limit must be an integer 0-100"}), 400
    return jsonify({"success": ok, "max_limit": limit})


@app.route("/api/charging/mode", methods=["POST"])
def api_set_mode():
    data = request.get_json(silent=True) or {}
    mode = data.get("mode", "")
    valid_modes = list(cc.MODES.keys())
    if mode not in valid_modes:
        return jsonify({"error": f"mode must be one of {valid_modes}"}), 400
    try:
        ok = cc.set_charging_mode(mode)
    except ValueError:
        return jsonify({"error": f"mode must be one of {valid_modes}"}), 400
    return jsonify({"success": ok, "mode": mode})


@app.route("/api/charging/temperature-check", methods=["POST"])
def api_temp_check():
    result = cc.check_temperature_protection()
    return jsonify(result)


# ── Configuration ─────────────────────────────────────────────────────────────

@app.route("/api/config", methods=["GET"])
def api_get_config():
    return jsonify(cc.load_config())


@app.route("/api/config", methods=["POST"])
def api_save_config():
    data = request.get_json(silent=True)
    if not isinstance(data, dict):
        return jsonify({"error": "JSON object required"}), 400
    ok = cc.save_config(data)
    return jsonify({"success": ok})


# ── Statistics ────────────────────────────────────────────────────────────────

@app.route("/api/stats/daily")
def api_stats_daily():
    days = request.args.get("days", 7, type=int)
    return jsonify(st.get_daily_stats(days))


@app.route("/api/stats/weekly")
def api_stats_weekly():
    return jsonify(st.get_weekly_stats())


@app.route("/api/stats/monthly")
def api_stats_monthly():
    return jsonify(st.get_monthly_stats())


@app.route("/api/stats/snapshots")
def api_stats_snapshots():
    limit = request.args.get("limit", 60, type=int)
    return jsonify(st.get_recent_snapshots(limit))


@app.route("/api/stats/health")
def api_battery_health():
    return jsonify(st.get_battery_health_trend())


# ── Data export ───────────────────────────────────────────────────────────────

@app.route("/api/export/csv")
def api_export_csv():
    csv_data = st.export_csv()
    from flask import Response
    return Response(
        csv_data,
        mimetype="text/csv",
        headers={"Content-Disposition": "attachment; filename=charging_data.csv"},
    )


@app.route("/api/export/json")
def api_export_json():
    return jsonify(st.export_json())


# ── Charging sessions ─────────────────────────────────────────────────────────

@app.route("/api/sessions/start", methods=["POST"])
def api_session_start():
    data = request.get_json(silent=True) or {}
    level = data.get("level", cc.get_battery_status().get("capacity", 0))
    mode = data.get("mode", "normal")
    session_id = st.start_session(level, mode)
    return jsonify({"session_id": session_id})


@app.route("/api/sessions/end", methods=["POST"])
def api_session_end():
    data = request.get_json(silent=True) or {}
    try:
        session_id = int(data["session_id"])
    except (KeyError, ValueError):
        return jsonify({"error": "session_id required"}), 400
    battery = cc.get_battery_status()
    end_level = data.get("level", battery.get("capacity", 0))
    max_temp = data.get("max_temp", battery.get("temperature", 0))
    ok = st.end_session(session_id, end_level, max_temp)
    return jsonify({"success": ok})


# ── Modes listing ─────────────────────────────────────────────────────────────

@app.route("/api/modes")
def api_modes():
    config = cc.load_config()
    modes = config.get("modes", cc.MODES)
    return jsonify(modes)


# ── Entry point ───────────────────────────────────────────────────────────────

if __name__ == "__main__":
    config = cc.load_config()
    server_cfg = config.get("server", {})
    host = server_cfg.get("host", "0.0.0.0")
    port = server_cfg.get("port", 8080)
    debug = server_cfg.get("debug", False)
    logger.info("Starting ChargeControl server on %s:%s", host, port)
    app.run(host=host, port=port, debug=debug)
