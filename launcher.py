"""
ChargeControl – launcher.py
Checks required dependencies, installs them if missing, then starts server.py.
This script is called by service.sh and acts as the entry point for the module.
"""

import importlib
import logging
import os
import subprocess
import sys

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s %(levelname)s %(name)s: %(message)s",
)
logger = logging.getLogger("launcher")

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
REQUIRED_PACKAGES = ["flask", "flask-cors"]


def _check_package(package: str) -> bool:
    """Return True if the given package is importable."""
    # flask-cors is imported as flask_cors
    import_name = package.replace("-", "_")
    try:
        importlib.import_module(import_name)
        return True
    except ImportError:
        return False


def _install_packages(packages: list) -> bool:
    """Attempt to install missing packages via pip."""
    logger.info("Installing missing packages: %s", packages)
    try:
        result = subprocess.run(
            [sys.executable, "-m", "pip", "install", "--quiet"] + packages,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
            check=True,
        )
        logger.info("Packages installed successfully.")
        return True
    except subprocess.CalledProcessError as exc:
        stderr_output = exc.stderr.decode(errors="replace").strip() if exc.stderr else ""
        logger.error("pip install failed: %s", stderr_output or exc)
        return False
    except FileNotFoundError:
        logger.error("pip is not available for this Python interpreter.")
        return False


def ensure_dependencies() -> None:
    """Check and install all required packages before starting the server."""
    missing = [pkg for pkg in REQUIRED_PACKAGES if not _check_package(pkg)]
    if not missing:
        logger.info("All dependencies satisfied.")
        return
    logger.warning("Missing packages: %s – attempting installation.", missing)
    if not _install_packages(missing):
        logger.error(
            "Could not install required packages. "
            "Please run: pip install %s",
            " ".join(missing),
        )
        sys.exit(1)
    # Verify again after installation
    still_missing = [pkg for pkg in missing if not _check_package(pkg)]
    if still_missing:
        logger.error(
            "Packages still missing after install attempt: %s. Exiting.",
            still_missing,
        )
        sys.exit(1)
    logger.info("Dependencies ready.")


def start_server() -> None:
    """Launch server.py in the current process (replaces this process)."""
    server_script = os.path.join(BASE_DIR, "server.py")
    if not os.path.isfile(server_script):
        logger.error("server.py not found at %s. Exiting.", server_script)
        sys.exit(1)

    logger.info("Starting ChargeControl server: %s", server_script)
    os.chdir(BASE_DIR)
    # exec replaces the launcher process with the server process, keeping the
    # same PID that service.sh wrote to server.pid.
    os.execv(sys.executable, [sys.executable, server_script])


if __name__ == "__main__":
    logger.info("ChargeControl launcher starting (Python %s)", sys.version.split()[0])
    ensure_dependencies()
    start_server()
