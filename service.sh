#!/system/bin/sh
# ChargeControl – service.sh
# Runs after the Android system has fully booted.
# Starts the Python web server in the background.

MODDIR="${0%/*}"
LOG="$MODDIR/module.log"

log() { echo "[$(date '+%Y-%m-%d %H:%M:%S')] $*" >> "$LOG"; }

log "service.sh started"

# Wait for the system to settle before starting the server
sleep 15

# Check that Python 3 is available
if ! command -v python3 >/dev/null 2>&1; then
    log "ERROR: python3 not found. Trying python..."
    if ! command -v python >/dev/null 2>&1; then
        log "ERROR: No Python interpreter found. Exiting."
        exit 1
    fi
    PYTHON=python
else
    PYTHON=python3
fi

# Install / upgrade Flask if pip is available
if command -v pip3 >/dev/null 2>&1; then
    pip3 install flask flask-cors --quiet 2>>"$LOG" || true
elif command -v pip >/dev/null 2>&1; then
    pip install flask flask-cors --quiet 2>>"$LOG" || true
fi

# Start the server
cd "$MODDIR"
log "Starting ChargeControl server (PID will be written to server.pid)"
nohup "$PYTHON" server.py >> "$LOG" 2>&1 &
echo $! > "$MODDIR/server.pid"
log "Server started with PID $(cat "$MODDIR/server.pid")"
