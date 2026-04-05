#!/system/bin/sh
# ChargeControl – service.sh
# Runs after the Android system has fully booted.
# Starts the snapshot daemon via launcher.py in the background.
# The KernelSU WebUI (webroot/) handles all user interaction directly via exec().

MODDIR="${0%/*}"
LOG="$MODDIR/module.log"
PIDFILE="$MODDIR/daemon.pid"
LOCKDIR="$MODDIR/.service.lock"

# Maximum seconds to wait for a Python interpreter to become available.
MAX_WAIT=180
INTERVAL=2

log() { echo "[$(date '+%Y-%m-%d %H:%M:%S')] $*" >> "$LOG"; }

log "service.sh started"

# --- Prevent concurrent / duplicate starts ---
if ! mkdir "$LOCKDIR" 2>/dev/null; then
    log "INFO: Another instance is already starting (lock exists). Exiting."
    exit 0
fi
# Always release the lock on exit, regardless of how the script terminates.
trap 'rmdir "$LOCKDIR" 2>/dev/null' EXIT

# If the snapshot daemon recorded in daemon.pid is still alive, do not restart it.
if [ -f "$PIDFILE" ]; then
    OLD_PID="$(cat "$PIDFILE" 2>/dev/null)"
    if [ -n "$OLD_PID" ] && kill -0 "$OLD_PID" 2>/dev/null; then
        log "INFO: Snapshot daemon already running with PID $OLD_PID. Exiting."
        exit 0
    fi
fi

# Wait for the system to settle before starting the server.
sleep 15

# --- Locate a Python interpreter (retries to handle boot-time race conditions) ---
find_python() {
    # Prefer absolute Termux paths first; they are unaffected by a minimal root PATH.
    for candidate in \
        /data/data/com.termux/files/usr/bin/python3 \
        /data/data/com.termux/files/usr/bin/python \
        python3 python \
        /system/bin/python3 /system/bin/python
    do
        if [ -x "$candidate" ] || command -v "$candidate" >/dev/null 2>&1; then
            echo "$candidate"
            return 0
        fi
    done
    return 1
}

PYTHON=""
WAITED=0

while [ "$WAITED" -lt "$MAX_WAIT" ]; do
    PYTHON="$(find_python)"
    if [ -n "$PYTHON" ]; then
        break
    fi
    sleep "$INTERVAL"
    WAITED=$((WAITED + INTERVAL))
done

if [ -z "$PYTHON" ]; then
    log "ERROR: No Python interpreter found after waiting ${MAX_WAIT}s."
    log "       Searched: /data/data/com.termux/files/usr/bin/python3,"
    log "                 /data/data/com.termux/files/usr/bin/python,"
    log "                 python3, python, /system/bin/python3, /system/bin/python."
    log "       Install Python (e.g. via Termux: pkg install python) and retry."
    exit 1
fi

log "INFO: Using Python interpreter: $PYTHON ($($PYTHON --version 2>&1))"

# Start the snapshot daemon via launcher.py.
cd "$MODDIR" || exit 1
log "Starting ChargeControl snapshot daemon via launcher.py (PID will be written to daemon.pid)"
nohup "$PYTHON" launcher.py >> "$LOG" 2>&1 &
echo $! > "$PIDFILE"
log "Snapshot daemon started with PID $(cat "$PIDFILE")"
