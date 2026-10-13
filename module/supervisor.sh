#!/system/bin/sh
# ChargeControl supervisor —— 保证 battd 常驻，崩溃/被 OTA 替换后自动拉起
# 用法: supervisor.sh start|stop|status
DIR=/data/adb/battery-manager
BIN=$DIR/battd
PIDF=$DIR/battd.pid
SUPF=$DIR/supervisor.pid
LOGF=$DIR/supervisor.log

log() { printf '%s %s\n' "$(date '+%Y-%m-%d %H:%M:%S')" "$*" >> "$LOGF" 2>/dev/null; }

running() {
  [ -f "$PIDF" ] || return 1
  p=$(cat "$PIDF" 2>/dev/null)
  [ -n "$p" ] && [ -d "/proc/$p" ] || return 1
  tr '\0' ' ' < "/proc/$p/cmdline" 2>/dev/null | grep -q battd || return 1
  return 0
}

start_battd() {
  [ -x "$BIN" ] || { log "bin missing: $BIN"; return 1; }
  if running; then kill "$(cat $PIDF)" 2>/dev/null; sleep 1; fi
  pkill -f "$BIN" 2>/dev/null
  sleep 1
  "$BIN" >> "$DIR/battd.stdout" 2>&1 &
  echo $! > "$PIDF"
  log "started pid=$(cat $PIDF)"
}

case "$1" in
  start)
    if [ -f "$SUPF" ] && [ -d "/proc/$(cat $SUPF 2>/dev/null)" ]; then
      log "supervisor already running"; exit 0
    fi
    echo $$ > "$SUPF"
    log "supervisor start"
    while true; do
      running || start_battd
      sleep 15
    done
    ;;
  stop)
    [ -f "$SUPF" ] && kill "$(cat $SUPF)" 2>/dev/null
    rm -f "$SUPF"
    running && kill "$(cat $PIDF)" 2>/dev/null
    log "supervisor stop"
    ;;
  status)
    if running; then echo "battd: running pid=$(cat $PIDF)"; else echo "battd: stopped"; fi
    if [ -f "$SUPF" ] && [ -d "/proc/$(cat $SUPF 2>/dev/null)" ]; then
      echo "supervisor: running pid=$(cat $SUPF)"
    else echo "supervisor: stopped"; fi
    ;;
  *) echo "usage: $0 start|stop|status"; exit 1;;
esac
