"""
充电控制 - Flask Web 服务器
提供 REST API 并托管 Web 界面。
"""

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
WEBROOT = os.path.join(BASE_DIR, "webroot")

app = Flask(__name__, static_folder=WEBROOT, static_url_path="")
CORS(app)

# ── 初始化数据库 ──────────────────────────────────────────────────────────────
st.init_db()

# ── 后台快照采集器 ────────────────────────────────────────────────────────────
_snapshot_interval = 30  # 秒


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
            logger.warning("快照记录出错: %s", exc)
        time.sleep(_snapshot_interval)


_snapshot_thread = threading.Thread(target=_snapshot_worker, daemon=True)
_snapshot_thread.start()


# ── 静态页面 ─────────────────────────────────────────────────────────────────

@app.route("/")
def index():
    return send_from_directory(WEBROOT, "index.html")


# ── 电池 / 状态 ─────────────────────────────────────────────────────────────

@app.route("/api/status")
def api_status():
    """返回实时电池状态。"""
    return jsonify(cc.get_battery_status())


@app.route("/api/settings")
def api_settings():
    """返回当前配置及实时电池数据。"""
    return jsonify(cc.get_all_settings())


# ── 充电控制 ─────────────────────────────────────────────────────────────────

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
        return jsonify({"error": "limit 必须为 0-100 之间的整数"}), 400
    try:
        ok = cc.set_charge_limit(limit)
    except ValueError:
        return jsonify({"error": "limit 必须为 0-100 之间的整数"}), 400
    return jsonify({"success": ok, "max_limit": limit})


@app.route("/api/charging/mode", methods=["POST"])
def api_set_mode():
    data = request.get_json(silent=True) or {}
    mode = data.get("mode", "")
    valid_modes = list(cc.MODES.keys())
    if mode not in valid_modes:
        return jsonify({"error": f"mode 必须为以下值之一: {valid_modes}"}), 400
    try:
        ok = cc.set_charging_mode(mode)
    except ValueError:
        return jsonify({"error": f"mode 必须为以下值之一: {valid_modes}"}), 400
    return jsonify({"success": ok, "mode": mode})


@app.route("/api/charging/temperature-check", methods=["POST"])
def api_temp_check():
    result = cc.check_temperature_protection()
    return jsonify(result)


# ── 配置管理 ─────────────────────────────────────────────────────────────────

@app.route("/api/config", methods=["GET"])
def api_get_config():
    return jsonify(cc.load_config())


@app.route("/api/config", methods=["POST"])
def api_save_config():
    data = request.get_json(silent=True)
    if not isinstance(data, dict):
        return jsonify({"error": "需要 JSON 对象"}), 400
    ok = cc.save_config(data)
    return jsonify({"success": ok})


# ── 统计数据 ────────────────────────────────────────────────────────────────

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


# ── 数据导出 ─────────────────────────────────────────────────────────────────

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


# ── 充电会话 ─────────────────────────────────────────────────────────────────

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
        return jsonify({"error": "需要 session_id"}), 400
    battery = cc.get_battery_status()
    end_level = data.get("level", battery.get("capacity", 0))
    max_temp = data.get("max_temp", battery.get("temperature", 0))
    ok = st.end_session(session_id, end_level, max_temp)
    return jsonify({"success": ok})


# ── 充电模式列表 ─────────────────────────────────────────────────────────────

@app.route("/api/modes")
def api_modes():
    config = cc.load_config()
    modes = config.get("modes", cc.MODES)
    return jsonify(modes)


# ── 程序入口 ─────────────────────────────────────────────────────────────────

if __name__ == "__main__":
    config = cc.load_config()
    server_cfg = config.get("server", {})
    host = server_cfg.get("host", "0.0.0.0")
    port = server_cfg.get("port", 8080)
    debug = server_cfg.get("debug", False)
    logger.info("正在启动充电控制服务器，地址: %s:%s", host, port)
    app.run(host=host, port=port, debug=debug)
