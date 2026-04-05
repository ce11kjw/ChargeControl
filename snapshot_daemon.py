"""
充电控制 - 后台快照守护进程
在 KernelSU WebUI 模式下运行，周期性记录电池快照到 SQLite 数据库。
不依赖 Flask，与 KernelSU 原生 WebUI（通过 exec() 交互）配合使用。
"""

import logging
import signal
import time

import charge_control as cc
import stats as st

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s %(levelname)s %(name)s: %(message)s",
)
logger = logging.getLogger(__name__)

SNAPSHOT_INTERVAL = 30  # 秒

_running = True


def _handle_signal(signum, frame):
    global _running
    logger.info("收到信号 %s，正在停止快照守护进程…", signum)
    _running = False


def run():
    global _running
    signal.signal(signal.SIGTERM, _handle_signal)
    signal.signal(signal.SIGINT, _handle_signal)

    logger.info("ChargeControl 快照守护进程启动")
    st.init_db()
    logger.info("数据库初始化完成，开始采集电池快照（每 %ds 一次）", SNAPSHOT_INTERVAL)

    while _running:
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
        time.sleep(SNAPSHOT_INTERVAL)

    logger.info("快照守护进程已停止")


if __name__ == "__main__":
    run()
