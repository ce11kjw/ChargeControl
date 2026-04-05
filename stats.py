"""
充电控制 - 统计与分析模块
记录充电会话并提供汇总报告。
"""

import sqlite3
import csv
import io
import os
import logging
from datetime import datetime, timedelta

logger = logging.getLogger(__name__)

DB_PATH = os.path.join(os.path.dirname(__file__), "chargecontrol.db")


def _get_conn() -> sqlite3.Connection:
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    return conn


def init_db() -> None:
    """如果数据库表不存在则创建。"""
    with _get_conn() as conn:
        conn.executescript("""
            CREATE TABLE IF NOT EXISTS charging_sessions (
                id          INTEGER PRIMARY KEY AUTOINCREMENT,
                start_time  TEXT NOT NULL,
                end_time    TEXT,
                start_level INTEGER NOT NULL,
                end_level   INTEGER,
                mode        TEXT NOT NULL DEFAULT 'normal',
                max_temp    REAL,
                duration_s  INTEGER,
                efficiency  REAL
            );

            CREATE TABLE IF NOT EXISTS battery_snapshots (
                id          INTEGER PRIMARY KEY AUTOINCREMENT,
                timestamp   TEXT NOT NULL,
                capacity    INTEGER,
                temperature REAL,
                voltage_mv  REAL,
                current_ma  REAL,
                status      TEXT,
                mode        TEXT
            );
        """)


def record_snapshot(capacity: int, temperature: float, voltage_mv: float | None,
                    current_ma: float | None, status: str, mode: str) -> int:
    """将实时电池快照插入数据库。"""
    now = datetime.utcnow().isoformat()
    with _get_conn() as conn:
        cur = conn.execute(
            """INSERT INTO battery_snapshots
               (timestamp, capacity, temperature, voltage_mv, current_ma, status, mode)
               VALUES (?, ?, ?, ?, ?, ?, ?)""",
            (now, capacity, temperature, voltage_mv, current_ma, status, mode),
        )
        return cur.lastrowid


def start_session(start_level: int, mode: str) -> int:
    """记录充电会话的开始。"""
    now = datetime.utcnow().isoformat()
    with _get_conn() as conn:
        cur = conn.execute(
            "INSERT INTO charging_sessions (start_time, start_level, mode) VALUES (?, ?, ?)",
            (now, start_level, mode),
        )
        return cur.lastrowid


def end_session(session_id: int, end_level: int, max_temp: float) -> bool:
    """记录充电会话结束并计算衍生指标。"""
    now = datetime.utcnow().isoformat()
    with _get_conn() as conn:
        row = conn.execute(
            "SELECT start_time, start_level FROM charging_sessions WHERE id = ?",
            (session_id,),
        ).fetchone()
        if row is None:
            return False

        start_dt = datetime.fromisoformat(row["start_time"])
        end_dt = datetime.fromisoformat(now)
        duration_s = int((end_dt - start_dt).total_seconds())

        delta_level = end_level - row["start_level"]
        efficiency = round(delta_level / (duration_s / 3600), 2) if duration_s > 0 else 0

        conn.execute(
            """UPDATE charging_sessions
               SET end_time=?, end_level=?, max_temp=?, duration_s=?, efficiency=?
               WHERE id=?""",
            (now, end_level, max_temp, duration_s, efficiency, session_id),
        )
    return True


def get_daily_stats(days: int = 7) -> list[dict]:
    """返回最近 N 天的按日汇总充电统计。"""
    since = (datetime.utcnow() - timedelta(days=days)).isoformat()
    with _get_conn() as conn:
        rows = conn.execute(
            """SELECT
                 DATE(start_time) AS day,
                 COUNT(*)         AS sessions,
                 AVG(efficiency)  AS avg_efficiency,
                 SUM(duration_s)  AS total_duration_s,
                 MAX(max_temp)    AS max_temp
               FROM charging_sessions
               WHERE start_time >= ? AND end_time IS NOT NULL
               GROUP BY DATE(start_time)
               ORDER BY day ASC""",
            (since,),
        ).fetchall()
    return [dict(r) for r in rows]


def get_weekly_stats() -> list[dict]:
    """返回最近 12 周的按周汇总统计。"""
    since = (datetime.utcnow() - timedelta(weeks=12)).isoformat()
    with _get_conn() as conn:
        rows = conn.execute(
            """SELECT
                 STRFTIME('%Y-W%W', start_time) AS week,
                 COUNT(*)                       AS sessions,
                 AVG(efficiency)                AS avg_efficiency,
                 SUM(duration_s)                AS total_duration_s,
                 MAX(max_temp)                  AS max_temp
               FROM charging_sessions
               WHERE start_time >= ? AND end_time IS NOT NULL
               GROUP BY week
               ORDER BY week ASC""",
            (since,),
        ).fetchall()
    return [dict(r) for r in rows]


def get_monthly_stats() -> list[dict]:
    """返回最近 12 个月的按月汇总统计。"""
    since = (datetime.utcnow() - timedelta(days=365)).isoformat()
    with _get_conn() as conn:
        rows = conn.execute(
            """SELECT
                 STRFTIME('%Y-%m', start_time) AS month,
                 COUNT(*)                      AS sessions,
                 AVG(efficiency)               AS avg_efficiency,
                 SUM(duration_s)               AS total_duration_s,
                 MAX(max_temp)                 AS max_temp
               FROM charging_sessions
               WHERE start_time >= ? AND end_time IS NOT NULL
               GROUP BY month
               ORDER BY month ASC""",
            (since,),
        ).fetchall()
    return [dict(r) for r in rows]


def get_recent_snapshots(limit: int = 60) -> list[dict]:
    """返回最近的电池快照记录。"""
    with _get_conn() as conn:
        rows = conn.execute(
            "SELECT * FROM battery_snapshots ORDER BY timestamp DESC LIMIT ?",
            (limit,),
        ).fetchall()
    return [dict(r) for r in reversed(rows)]


def get_battery_health_trend() -> dict:
    """根据电压/电量数据估算电池健康趋势。"""
    with _get_conn() as conn:
        row = conn.execute(
            """SELECT
                 AVG(temperature) AS avg_temp,
                 MAX(temperature) AS max_temp,
                 COUNT(*)         AS total_snapshots
               FROM battery_snapshots""",
        ).fetchone()
        session_count = conn.execute(
            "SELECT COUNT(*) AS cnt FROM charging_sessions WHERE end_time IS NOT NULL"
        ).fetchone()

    avg_temp = row["avg_temp"] or 0
    max_temp = row["max_temp"] or 0

    # 简单启发式：根据高温扣减健康评分
    health_score = 100
    if avg_temp > 38:
        health_score -= 10
    if max_temp > 44:
        health_score -= 15
    health_score = max(0, health_score)

    return {
        "estimated_health": health_score,
        "avg_temp": round(avg_temp, 1),
        "max_temp": round(max_temp, 1),
        "total_snapshots": row["total_snapshots"],
        "total_sessions": session_count["cnt"],
    }


def export_csv() -> str:
    """将所有充电会话导出为 CSV 字符串。"""
    with _get_conn() as conn:
        rows = conn.execute("SELECT * FROM charging_sessions ORDER BY start_time").fetchall()

    output = io.StringIO()
    if rows:
        writer = csv.DictWriter(output, fieldnames=rows[0].keys())
        writer.writeheader()
        for row in rows:
            writer.writerow(dict(row))
    return output.getvalue()


def export_json() -> list[dict]:
    """将所有充电会话导出为字典列表（可序列化为 JSON）。"""
    with _get_conn() as conn:
        rows = conn.execute("SELECT * FROM charging_sessions ORDER BY start_time").fetchall()
    return [dict(r) for r in rows]


def prune_old_data(retention_days: int = 90) -> int:
    """删除超过保留天数的记录，并返回已删除的行数。"""
    cutoff = (datetime.utcnow() - timedelta(days=retention_days)).isoformat()
    with _get_conn() as conn:
        cur1 = conn.execute(
            "DELETE FROM charging_sessions WHERE start_time < ?", (cutoff,)
        )
        cur2 = conn.execute(
            "DELETE FROM battery_snapshots WHERE timestamp < ?", (cutoff,)
        )
    return cur1.rowcount + cur2.rowcount
