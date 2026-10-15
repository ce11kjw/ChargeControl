#!/system/bin/sh
# ChargeControl supervisor —— 保证 battd 常驻
#
# v1.10.0 修复（真机实测踩到的两个坑）：
#   1) PID 复用误判：原逻辑只判断 `[ -d /proc/$PID ]`，
#      而 Android PID 循环很快，旧 PID 被别的进程占用时会误认为
#      "supervisor 还在跑" → 直接 exit 0 → battd 再无人守护。
#      ⇒ 改为读取 /proc/$PID/cmdline 验证确实是本脚本。
#   2) 进程被回收：service.sh 里 `supervisor.sh start &` 后台启动后脚本退出，
#      子进程可能随之被清理。⇒ supervisor 自己写入 PID 文件，
#      且 service.sh 用 setsid 脱离会话。
#
# 用法: supervisor.sh start|stop|status|restart

DIR=/data/adb/battery-manager
BIN=$DIR/battd
PIDF=$DIR/battd.pid
SUPF=$DIR/supervisor.pid
LOGF=$DIR/supervisor.log
SELF="supervisor.sh"

log() { printf '%s %s\n' "$(date '+%Y-%m-%d %H:%M:%S')" "$*" >> "$LOGF" 2>/dev/null; }

# 进程身份校验：PID 存在 **且** cmdline 含预期关键字
proc_is() {
  _p="$1"; _kw="$2"
  [ -n "$_p" ] || return 1
  [ -d "/proc/$_p" ] || return 1
  tr '\0' ' ' < "/proc/$_p/cmdline" 2>/dev/null | grep -q "$_kw" || return 1
  return 0
}

battd_running() {
  proc_is "$(cat "$PIDF" 2>/dev/null)" "battd"
}

supervisor_running() {
  proc_is "$(cat "$SUPF" 2>/dev/null)" "$SELF"
}

start_battd() {
  [ -x "$BIN" ] || { log "bin missing: $BIN"; return 1; }
  if battd_running; then kill "$(cat $PIDF)" 2>/dev/null; sleep 1; fi
  pkill -f "$BIN" 2>/dev/null
  sleep 1
  setsid "$BIN" >> "$DIR/battd.stdout" 2>&1 &
  echo $! > "$PIDF"
  log "started battd pid=$(cat $PIDF)"
}

case "$1" in
  start)
    # ★ 关键修复：用 cmdline 验证，而不是只看 /proc 目录存在
    if supervisor_running; then
      log "supervisor already running (pid=$(cat $SUPF))"
      exit 0
    fi
    # 清理失效的 PID 文件后重新守护
    rm -f "$SUPF"
    echo $$ > "$SUPF"
    log "supervisor start pid=$$"
    while true; do
      battd_running || start_battd
      sleep 15
    done
    ;;
  stop)
    if supervisor_running; then kill "$(cat $SUPF)" 2>/dev/null; fi
    rm -f "$SUPF"
    if battd_running; then kill "$(cat $PIDF)" 2>/dev/null; fi
    rm -f "$PIDF"
    log "supervisor stop"
    ;;
  restart)
    "$0" stop; sleep 1; setsid "$0" start >/dev/null 2>&1 &
    ;;
  status)
    if battd_running; then echo "battd: running pid=$(cat $PIDF)"; else echo "battd: stopped"; fi
    if supervisor_running; then echo "supervisor: running pid=$(cat $SUPF)"; else echo "supervisor: stopped"; fi
    ;;
  *)
    echo "usage: $0 start|stop|restart|status"; exit 1
    ;;
esac
