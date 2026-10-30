#!/system/bin/sh
# ═══════════════════════════════════════════════════════════════
# ChargeControl 热更新脚本（由 customize.sh 后台拉起）
#
# 参考：nyehueh/File Manager v7.3.1 的 update.sh 思路
#   customize.sh 末尾 nohup 启动本脚本，传入 $MODPATH（解压目录）
#   本脚本：等安装器退出 → 杀进程 → cp -a 整个模块目录 → 启动新的
#
# ⚠️ 与 FileManager 的关键差异（实测踩到）：
#   FileManager 用 `PARENT_PID=$PPID; while [ -d /proc/$PARENT_PID ]` 等父进程退出。
#   但实测：nohup/setsid 启动后，$PPID 立刻变成 1（init 收养），
#   而 /proc/1 永远存在 ⇒ 死循环。
#   （FileManager 能用，是因为它的 customize.sh 被 source，
#     $PPID 恰好是安装器；不同管理器行为不同，不可依赖。）
#
#   ⇒ 改用更可靠的判据：
#     1. 固定等待 3 秒（让安装器完成收尾）
#     2. 轮询检查「安装器进程」是否还在（pm/ksud/apd/magisk 安装流程）
#     3. 再用「文件大小稳定」双重确认（管理器不会还在写 $MODPATH）
#
# 用法: update.sh <解压后的模块目录>
# ═══════════════════════════════════════════════════════════════
set -u

SRC="${1:-}"
MOD_DIR=/data/adb/modules/chargecontrol
DATA_DIR=/data/adb/battery-manager
LOG=$DATA_DIR/hot-update.log

mkdir -p "$DATA_DIR" 2>/dev/null
log() { echo "$(date '+%Y-%m-%d %H:%M:%S') $*" >> "$LOG" 2>/dev/null; }

# ── 0. 参数校验 ──
if [ -z "$SRC" ] || [ ! -d "$SRC" ]; then
  log "FATAL: 无效的源目录 '$SRC'"
  exit 1
fi
if [ ! -f "$SRC/module.prop" ] || [ ! -f "$SRC/bin/battd" ]; then
  log "FATAL: 源目录结构不完整（缺 module.prop 或 bin/battd）"
  exit 1
fi

NEW_VER=$(grep '^version=' "$SRC/module.prop" 2>/dev/null | cut -d= -f2)
log "=== 热更新开始 → $NEW_VER ==="
log "源目录: $SRC  (PPID=$PPID)"

# ── 1. 等安装器收尾 ──
#    不能只靠 $PPID（实测会被 init 收养成 1）
log "等待安装器收尾…"

# 1a) 先固定等 3 秒（安装器通常立即结束，但可能还在写文件）
sleep 3

# 1b) 检查常见安装器进程是否还在（最多再等 30 秒）
#     pm install / ksud module install / apd install / magisk --install-module
_w=0
while [ $_w -lt 30 ]; do
  _found=""
  for _p in /proc/[0-9]*; do
    _pid=${_p#/proc/}
    [ "$_pid" = "1" ] && continue
    [ "$_pid" = "$$" ] && continue
    [ -r "$_p/cmdline" ] || continue
    _cmd=$(tr '\0' ' ' < "$_p/cmdline" 2>/dev/null)
    case "$_cmd" in
      *"--install-module"*|*"module install"*|*"apd install"*|*"pm install"*)
        _found="$_pid:$_cmd"
        break
        ;;
    esac
  done
  [ -z "$_found" ] && break
  sleep 1
  _w=$((_w+1))
done
[ $_w -gt 0 ] && log "  额外等了 ${_w}s 等安装器退出"

# 1c) 文件稳定性检查：连续 2 次采样，大小/时间戳不变
_prev=""
_stable=0
_s=0
while [ $_s -lt 10 ]; do
  _cur=$(find "$SRC" -type f -exec stat -c '%s:%Y' {} \; 2>/dev/null | sort | md5sum 2>/dev/null | cut -d' ' -f1)
  if [ "$_cur" = "$_prev" ] && [ -n "$_cur" ]; then
    _stable=$((_stable+1))
    [ $_stable -ge 2 ] && break
  else
    _stable=0
  fi
  _prev=$_cur
  sleep 0.5
  _s=$((_s+1))
done
log "  文件已稳定（采样 ${_s} 次）"

# ── 2. 停 supervisor（否则替换期间它会拉起旧 battd）──
log "停止 supervisor"
for _p in /proc/[0-9]*; do
  _pid=${_p#/proc/}
  [ -r "$_p/cmdline" ] || continue
  _cmd=$(tr '\0' ' ' < "$_p/cmdline" 2>/dev/null)
  case "$_cmd" in
    *battery-manager/supervisor.sh*)
      kill -TERM "$_pid" 2>/dev/null
      log "  supervisor pid=$_pid → TERM"
      ;;
  esac
done
sleep 1

# ── 3. 停 battd ──
log "停止 battd"
_old_pids=""
for _p in /proc/[0-9]*; do
  _pid=${_p#/proc/}
  [ -r "$_p/cmdline" ] || continue
  _cmd=$(tr '\0' ' ' < "$_p/cmdline" 2>/dev/null)
  case "$_cmd" in
    *battery-manager/battd*|*chargecontrol/bin/battd*)
      _old_pids="$_old_pids $_pid"
      kill -TERM "$_pid" 2>/dev/null
      ;;
  esac
done
log "  battd 旧进程:${_old_pids:- 无}"

_t=60
while [ $_t -gt 0 ]; do
  _alive=0
  for _p in /proc/[0-9]*; do
    _pid=${_p#/proc/}
    [ -r "$_p/cmdline" ] || continue
    _cmd=$(tr '\0' ' ' < "$_p/cmdline" 2>/dev/null)
    case "$_cmd" in
      *battery-manager/battd*) _alive=1; break ;;
    esac
  done
  [ $_alive -eq 0 ] && break
  sleep 1
  _t=$((_t-1))
done
# v2.0.1: 只在确实还有残留时才 KILL（避免日志刷两条「停止 battd」）
_still=""
for _p in /proc/[0-9]*; do
  _pid=${_p#/proc/}
  [ -r "$_p/cmdline" ] || continue
  _cmd=$(tr '\0' ' ' < "$_p/cmdline" 2>/dev/null)
  case "$_cmd" in
    *battery-manager/battd*) _still="$_still $_pid" ;;
  esac
done
if [ -n "$_still" ]; then
  for _pid in $_still; do
    kill -KILL "$_pid" 2>/dev/null
    log "  强制 KILL 残留 pid=$_pid"
  done
  sleep 1
fi

# ── 4. 替换模块目录（旧目录先备份）──
log "备份旧模块目录"
rm -rf "$MOD_DIR.pre-hot-update" 2>/dev/null
if [ -d "$MOD_DIR" ]; then
  cp -a "$MOD_DIR" "$MOD_DIR.pre-hot-update" 2>/dev/null \
    && log "  → $MOD_DIR.pre-hot-update"
fi

log "替换模块目录"
# ⚠️ v2.0.1 修复：这里原来有一行 `rm -f "$SRC/update.sh"`，
# 本意是「不要把 update.sh 带进目标」，但删的是**解压目录**里的文件，
# 导致 cp -a 后模块目录缺少 update.sh ⇒ 下次热更新失效。
# update.sh 是热更新的执行者，应该被复制进模块目录。
rm -rf "$MOD_DIR.new" 2>/dev/null
if ! cp -a "$SRC" "$MOD_DIR.new" 2>>"$LOG"; then
  log "FATAL: 复制失败，旧目录保持不动"
  rm -rf "$MOD_DIR.new" 2>/dev/null
  exit 1
fi
rm -rf "$MOD_DIR" 2>/dev/null
if ! mv "$MOD_DIR.new" "$MOD_DIR" 2>>"$LOG"; then
  log "FATAL: 重命名失败，回滚"
  [ -d "$MOD_DIR.pre-hot-update" ] && mv "$MOD_DIR.pre-hot-update" "$MOD_DIR"
  exit 1
fi
log "  新目录就位"

# ── 5. 同步运行目录 ──
log "同步运行目录"
mkdir -p "$DATA_DIR/webroot"
if [ -f "$MOD_DIR/bin/battd" ]; then
  cp -f "$MOD_DIR/bin/battd" "$DATA_DIR/battd" 2>>"$LOG"
  chmod 755 "$DATA_DIR/battd" 2>/dev/null
  log "  battd 已同步 ($(stat -c%s "$DATA_DIR/battd" 2>/dev/null) B)"
fi
if [ -d "$MOD_DIR/webroot" ]; then
  for _f in "$MOD_DIR"/webroot/*; do
    [ -f "$_f" ] || continue
    cp -f "$_f" "$DATA_DIR/webroot/$(basename "$_f")" 2>>"$LOG"
  done
  log "  webroot 已同步 ($(ls -1 "$DATA_DIR/webroot" 2>/dev/null | wc -l) 个文件)"
fi
if [ -f "$MOD_DIR/supervisor.sh" ]; then
  cp -f "$MOD_DIR/supervisor.sh" "$DATA_DIR/supervisor.sh" 2>>"$LOG"
  chmod 755 "$DATA_DIR/supervisor.sh" 2>/dev/null
fi
chmod 755 "$MOD_DIR"/bin/battd 2>/dev/null
chmod 755 "$MOD_DIR"/*.sh 2>/dev/null

# v2.0.1: 清理旧版本残留的 APK（v1.19.5 起不再打包 App）
#   老用户升级后 /data/adb/battery-manager/ChargeControl.apk 会一直占 14MB
if [ -f "$DATA_DIR/ChargeControl.apk" ]; then
  _apk_size=$(stat -c%s "$DATA_DIR/ChargeControl.apk" 2>/dev/null || echo 0)
  rm -f "$DATA_DIR/ChargeControl.apk" 2>/dev/null
  log "  清理旧 APK 残留（${_apk_size} 字节）"
fi
# 模块目录里也不该有
rm -f "$MOD_DIR/ChargeControl.apk" 2>/dev/null

# ── 6. 启动新进程 ──
log "启动新 battd"
rm -f "$DATA_DIR/battd.pid" "$DATA_DIR/supervisor.pid" 2>/dev/null
cd "$DATA_DIR" || exit 1

if [ -x "$DATA_DIR/supervisor.sh" ]; then
  setsid sh "$DATA_DIR/supervisor.sh" start >> "$DATA_DIR/supervisor.log" 2>&1 &
  log "  supervisor 已拉起"
  sleep 3
fi

if ! pgrep -f "$DATA_DIR/battd" >/dev/null 2>&1; then
  log "  直接启动 battd"
  setsid "$DATA_DIR/battd" >> "$DATA_DIR/battd.stdout" 2>&1 &
fi
sleep 3

# ── 7. 验证 ──
_new_pid=""
for _p in /proc/[0-9]*; do
  _pid=${_p#/proc/}
  [ -r "$_p/cmdline" ] || continue
  _cmd=$(tr '\0' ' ' < "$_p/cmdline" 2>/dev/null)
  case "$_cmd" in
    *battery-manager/battd*) _new_pid=$_pid; break ;;
  esac
done

if [ -n "$_new_pid" ]; then
  log "SUCCESS: 新 battd pid=$_new_pid, version=$NEW_VER"
  rm -rf "$MOD_DIR.pre-hot-update" 2>/dev/null   # 成功就清备份
  log "=== 热更新完成 ==="
else
  log "FAIL: 新进程未启动，回滚"
  if [ -d "$MOD_DIR.pre-hot-update" ]; then
    rm -rf "$MOD_DIR" && mv "$MOD_DIR.pre-hot-update" "$MOD_DIR"
    cp -f "$MOD_DIR/bin/battd" "$DATA_DIR/battd" 2>/dev/null
    nohup "$DATA_DIR/battd" >> "$DATA_DIR/battd.stdout" 2>&1 < /dev/null &
    log "  已回滚"
  fi
  exit 1
fi
exit 0
