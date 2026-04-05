"""
ChargeControl launcher
KernelSU WebUI 模式：启动后台快照守护进程，不再依赖 Flask。
WebUI 交互通过 exec() 直接与系统通信（见 webroot/script.js）。
"""

import os
import runpy

BASE_DIR = os.path.dirname(os.path.abspath(__file__))


def _start_daemon():
    daemon_path = os.path.join(BASE_DIR, "snapshot_daemon.py")
    print(f"[launcher] Starting snapshot daemon: {daemon_path}")
    runpy.run_path(daemon_path, run_name="__main__")


if __name__ == "__main__":
    _start_daemon()