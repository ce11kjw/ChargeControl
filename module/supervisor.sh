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

# ── 采样器守护 ──
# 用「数据新鲜度」判断而不是查进程名：pgrep -f 会匹配到调用方自己的命令行
# （今天已踩两次），而 power.json 超过 400s 没更新就一定有问题。
SAMPLER=$DIR/power-sampler.sh
SAMPF=$DIR/sampler.pid
sampler_alive() {
  _p=$(cat "$SAMPF" 2>/dev/null)
  [ -n "$_p" ] && [ -d "/proc/$_p" ] || return 1
  [ -f "$DIR/webroot/power.json" ] || return 1
  _now=$(date +%s)
  _mt=$(stat -c %Y "$DIR/webroot/power.json" 2>/dev/null)
  [ -n "$_mt" ] || return 1
  [ $((_now - _mt)) -lt 400 ]
}

start_sampler() {
  [ -x "$SAMPLER" ] || { log "sampler missing: $SAMPLER"; return 1; }
  # wrapper 自己写 pid，setsid 的 $! 不可靠
  setsid sh -c "echo \$\$ > '$SAMPF'; while true; do '$SAMPLER'; sleep 300; done" >/dev/null 2>&1 &
  log "started sampler"
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
      sampler_alive || start_sampler
      sleep 15
    done
    ;;
  stop)
    if supervisor_running; then kill "$(cat $SUPF)" 2>/dev/null; fi
    rm -f "$SUPF"
    if battd_running; then kill "$(cat $PIDF)" 2>/dev/null; fi
    rm -f "$PIDF"
    _sp=$(cat "$SAMPF" 2>/dev/null)
    [ -n "$_sp" ] && kill "$_sp" 2>/dev/null
    rm -f "$SAMPF"
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
