"""
ChargeControl launcher
启动 C HTTP 服务器（charge_control 二进制）以及后台快照守护进程。
"""

import os
import subprocess
import runpy

BASE_DIR = os.path.dirname(os.path.abspath(__file__))


def _start_http_server():
    binary = os.path.join(BASE_DIR, "charge_control")
    if not os.path.isfile(binary):
        print(f"[launcher] WARNING: charge_control binary not found at {binary}, skipping HTTP server start.")
        return None
    if not os.access(binary, os.X_OK):
        print(f"[launcher] WARNING: charge_control binary is not executable at {binary}, skipping.")
        return None
    print(f"[launcher] Starting HTTP server: {binary}")
    log_file = open(os.path.join(BASE_DIR, "module.log"), "a")
    proc = subprocess.Popen(
        [binary, BASE_DIR],
        stdout=log_file,
        stderr=subprocess.STDOUT,
    )
    print(f"[launcher] HTTP server started with PID {proc.pid}")
    return proc, log_file


def _start_daemon():
    daemon_path = os.path.join(BASE_DIR, "snapshot_daemon.py")
    print(f"[launcher] Starting snapshot daemon: {daemon_path}")
    runpy.run_path(daemon_path, run_name="__main__")


if __name__ == "__main__":
    _server = _start_http_server()
    _start_daemon()