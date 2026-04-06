#!/system/bin/sh
# ChargeControl – uninstall.sh
# Called by KernelSU when the module is removed.

MODDIR="${0%/*}"
LOG="$MODDIR/module.log"

log() { echo "[$(date '+%Y-%m-%d %H:%M:%S')] $*" >> "$LOG"; }

log "uninstall.sh started"

# Stop the running server if any
if [ -f "$MODDIR/server.pid" ]; then
    PID=$(cat "$MODDIR/server.pid")
    kill "$PID" 2>/dev/null
    rm -f "$MODDIR/server.pid"
    log "Stopped server (PID $PID)"
fi

# Also attempt to kill by name in case PID file is stale
pkill -f "python.*server.py" 2>/dev/null || true

# Remove log files and database
rm -f "$MODDIR/module.log"
rm -f "$MODDIR/chargecontrol.db"

echo "[$(date '+%Y-%m-%d %H:%M:%S')] ChargeControl uninstalled successfully."
