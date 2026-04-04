#!/system/bin/sh
# ChargeControl – service.sh
# Runs after the Android system has fully booted.
# Starts the Python web server via launcher.py in the background.

MODDIR="${0%/*}"
LOG="$MODDIR/module.log"

log() { echo "[$(date '+%Y-%m-%d %H:%M:%S')] $*" >> "$LOG"; }

log "service.sh started"

# Wait for the system to settle before starting the server
sleep 15

# Locate a Python interpreter – check common paths on Android and Linux
PYTHON=""
for candidate in python3 python /system/bin/python3 /system/bin/python \
                 /data/data/com.termux/files/usr/bin/python3 \
                 /data/data/com.termux/files/usr/bin/python; do
    # Accept the candidate if it is on PATH or is a directly executable file
    if command -v "$candidate" >/dev/null 2>&1 || [ -x "$candidate" ]; then
        PYTHON="$candidate"
        break
    fi
done

if [ -z "$PYTHON" ]; then
    log "ERROR: No Python interpreter found. Searched: python3, python,"
    log "       /system/bin/python3, /system/bin/python, Termux paths."
    log "       Install Python (e.g. via Termux: pkg install python) and retry."
    exit 1
fi

log "INFO: Using Python interpreter: $PYTHON ($($PYTHON --version 2>&1))"

# Start the server via launcher.py (handles dependency checks automatically)
cd "$MODDIR"
log "Starting ChargeControl server via launcher.py (PID will be written to server.pid)"
nohup "$PYTHON" launcher.py >> "$LOG" 2>&1 &
echo $! > "$MODDIR/server.pid"
log "Server started with PID $(cat "$MODDIR/server.pid")"
