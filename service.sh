#!/system/bin/sh
# ChargeControl – service.sh
# Runs after the Android system has fully booted.
# Starts the compiled charge_control binary in the background.

MODDIR="${0%/*}"
LOG="$MODDIR/module.log"
PIDFILE="$MODDIR/daemon.pid"
LOCKDIR="$MODDIR/.service.lock"

log() { echo "[$(date '+%Y-%m-%d %H:%M:%S')] $*" >> "$LOG"; }

log "service.sh started"

# --- Prevent concurrent / duplicate starts ---
if ! mkdir "$LOCKDIR" 2>/dev/null; then
    log "INFO: Another instance is already starting (lock exists). Exiting."
    exit 0
fi
# Always release the lock on exit, regardless of how the script terminates.
trap 'rmdir "$LOCKDIR" 2>/dev/null' EXIT

# If charge_control recorded in daemon.pid is still alive, do not restart it.
if [ -f "$PIDFILE" ]; then
    OLD_PID="$(cat "$PIDFILE" 2>/dev/null)"
    if [ -n "$OLD_PID" ] && kill -0 "$OLD_PID" 2>/dev/null; then
        log "INFO: charge_control already running with PID $OLD_PID. Exiting."
        exit 0
    fi
fi

# Wait for the system to settle before starting the server.
sleep 15

# Verify the binary exists and is executable.
if [ ! -x "$MODDIR/charge_control" ]; then
    log "ERROR: charge_control binary not found or not executable at $MODDIR/charge_control"
    log "       Please build the module (run 'make' in the module source directory)."
    exit 1
fi

# Start the charge_control HTTP server + snapshot daemon.
cd "$MODDIR" || exit 1
log "Starting ChargeControl (PID will be written to daemon.pid)"
nohup "$MODDIR/charge_control" >> "$LOG" 2>&1 &
echo $! > "$PIDFILE"
log "charge_control started with PID $(cat "$PIDFILE")"
