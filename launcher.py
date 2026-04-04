"""
充电控制 – launcher.py
检查所需依赖，缺失时自动安装，然后启动 server.py。
本脚本由 service.sh 调用，作为模块的入口点。
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
    """若指定包可导入则返回 True。"""
    # flask-cors 的导入名为 flask_cors
    import_name = package.replace("-", "_")
    try:
        importlib.import_module(import_name)
        return True
    except ImportError:
        return False


def _install_packages(packages: list) -> bool:
    """尝试通过 pip 安装缺失的依赖包。"""
    logger.info("正在安装缺失的依赖包: %s", packages)
    try:
        result = subprocess.run(
            [sys.executable, "-m", "pip", "install", "--quiet"] + packages,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
            check=True,
        )
        logger.info("依赖包安装成功。")
        return True
    except subprocess.CalledProcessError as exc:
        stderr_output = exc.stderr.decode(errors="replace").strip() if exc.stderr else ""
        logger.error("pip 安装失败: %s", stderr_output or exc)
        return False
    except FileNotFoundError:
        logger.error("该 Python 解释器不可用 pip。")
        return False


def ensure_dependencies() -> None:
    """在启动服务器前检查并安装所有必需的依赖包。"""
    missing = [pkg for pkg in REQUIRED_PACKAGES if not _check_package(pkg)]
    if not missing:
        logger.info("所有依赖已满足。")
        return
    logger.warning("缺失的依赖包: %s – 正在尝试安装。", missing)
    if not _install_packages(missing):
        logger.error(
            "无法安装所需依赖包。"
            "请手动运行: pip install %s",
            " ".join(missing),
        )
        sys.exit(1)
    # 安装后再次验证
    still_missing = [pkg for pkg in missing if not _check_package(pkg)]
    if still_missing:
        logger.error(
            "安装尝试后仍缺失以下包: %s。正在退出。",
            still_missing,
        )
        sys.exit(1)
    logger.info("依赖准备完毕。")


def start_server() -> None:
    """在当前进程中启动 server.py（替换本进程）。"""
    server_script = os.path.join(BASE_DIR, "server.py")
    if not os.path.isfile(server_script):
        logger.error("在 %s 未找到 server.py。正在退出。", server_script)
        sys.exit(1)

    logger.info("正在启动充电控制服务器: %s", server_script)
    os.chdir(BASE_DIR)
    # exec 将启动器进程替换为服务器进程，保持
    # service.sh 写入 server.pid 的相同 PID。
    os.execv(sys.executable, [sys.executable, server_script])


if __name__ == "__main__":
    logger.info("充电控制启动器正在初始化 (Python %s)", sys.version.split()[0])
    ensure_dependencies()
    start_server()
