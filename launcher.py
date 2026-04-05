"""
ChargeControl launcher
Installs required packages from requirements.txt, then starts server.py.
"""

import os
import runpy
import subprocess
import sys

BASE_DIR = os.path.dirname(os.path.abspath(__file__))


def _read_requirements():
    req_file = os.path.join(BASE_DIR, "requirements.txt")
    packages = []
    try:
        with open(req_file, "r") as f:
            for line in f:
                pkg = line.strip()
                if pkg and not pkg.startswith("#"):
                    packages.append(pkg)
    except OSError as exc:
        print(f"[launcher] WARNING: Cannot read requirements.txt: {exc}")
    return packages


def _install_packages(packages):
    if not packages:
        return
    print(f"[launcher] Installing packages: {packages}")
    cmd = [sys.executable, "-m", "pip", "install", "--no-cache-dir", "--quiet"] + packages
    result = subprocess.run(cmd)
    if result.returncode != 0:
        print(f"[launcher] ERROR: pip install failed with exit code {result.returncode}")
        sys.exit(result.returncode)
    print("[launcher] Package installation complete.")


def _start_server():
    server_path = os.path.join(BASE_DIR, "server.py")
    print(f"[launcher] Starting server: {server_path}")
    # runpy executes server.py as __main__, triggering its if __name__ == "__main__" block.
    runpy.run_path(server_path, run_name="__main__")


if __name__ == "__main__":
    packages = _read_requirements()
    _install_packages(packages)
    _start_server()