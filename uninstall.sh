#!/system/bin/sh
# ChargeControl – uninstall.sh
# Called by KernelSU when the module is removed.

MODDIR="${0%/*}"

# Stop the running server if any
if [ -f "$MODDIR/server.pid" ]; then
    PID=$(cat "$MODDIR/server.pid")
    kill "$PID" 2>/dev/null
    rm -f "$MODDIR/server.pid"
fi

# Also attempt to kill by name in case PID file is stale
pkill -f "python.*server.py" 2>/dev/null || true

# Remove log files
rm -f "$MODDIR/module.log"
rm -f "$MODDIR/chargecontrol.db"

echo "ChargeControl uninstalled successfully."
