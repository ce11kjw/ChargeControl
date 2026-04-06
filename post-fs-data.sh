#!/system/bin/sh
# Initialization script that runs after filesystem mount

MODDIR="${0%/*}"
LOG="$MODDIR/module.log"

log() { echo "[$(date '+%Y-%m-%d %H:%M:%S')] $*" >> "$LOG"; }

log "post-fs-data.sh started"
