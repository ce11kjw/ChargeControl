#!/system/bin/sh
# Initialization script that runs after filesystem mount

MODDIR="${0%/*}"
LOG="$MODDIR/module.log"

log() { echo "[$(date '+%Y-%m-%d %H:%M:%S')] $*" >> "$LOG"; }

log "post-fs-data.sh started"

# Ensure the charge_control binary is executable at boot
if [ -f "$MODDIR/charge_control" ]; then
    chmod 777 "$MODDIR/charge_control"
    log "INFO: chmod 777 applied to charge_control"
fi
