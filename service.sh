#!/system/bin/sh
# ChargeControl – service.sh
# Runs after the Android system has fully booted.
# Starts the chargecontrol C binary (HTTP server + snapshot daemon).

MODDIR="${0%/*}"
LOG="$MODDIR/module.log"
PIDFILE="$MODDIR/daemon.pid"
LOCKDIR="$MODDIR/.service.lock"
BINARY="$MODDIR/build/chargecontrol"

log() { echo "[$(date '+%Y-%m-%d %H:%M:%S')] $*" >> "$LOG"; }

log "service.sh started"

# --- Prevent concurrent / duplicate starts ---
if ! mkdir "$LOCKDIR" 2>/dev/null; then
    log "INFO: Another instance is already starting (lock exists). Exiting."
    exit 0
fi
# Always release the lock on exit, regardless of how the script terminates.
trap 'rmdir "$LOCKDIR" 2>/dev/null' EXIT

# If the server is already running, do not restart it.
if [ -f "$PIDFILE" ]; then
    OLD_PID="$(cat "$PIDFILE" 2>/dev/null)"
    if [ -n "$OLD_PID" ] && kill -0 "$OLD_PID" 2>/dev/null; then
        log "INFO: chargecontrol already running with PID $OLD_PID. Exiting."
        exit 0
    fi
fi

# Verify binary exists and is executable.
if [ ! -x "$BINARY" ]; then
    log "ERROR: chargecontrol binary not found at $BINARY"
    log "       Build it with: make  (or  make CROSS=aarch64-linux-android- for ARM64)"
    exit 1
fi

# Wait for the system to settle before starting the server.
sleep 15

cd "$MODDIR" || exit 1
log "Starting chargecontrol binary: $BINARY"
nohup "$BINARY" \
    --config "$MODDIR/config.json" \
    --db     "$MODDIR/chargecontrol.db" \
    --webroot "$MODDIR/webroot" \
    >> "$LOG" 2>&1 &
echo $! > "$PIDFILE"
log "chargecontrol started with PID $(cat "$PIDFILE")"
