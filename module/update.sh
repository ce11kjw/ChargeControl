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
# ══════════════════════════════════════════════════════════
# ★ v2.1.3: 临时/备份目录必须放在 modules 之外
#
# APatch/KernelSU 用 module.prop 的 id 作为 LazyColumn 的 key。
# 如果在 /data/adb/modules/ 下留了带 module.prop 的副本
# （如 chargecontrol.pre-v201 / .old-run / .new），
# 就会出现两个 id=chargecontrol ⇒ 管理器崩溃：
#   IllegalArgumentException: Key "chargecontrol" was already used
# ══════════════════════════════════════════════════════════
BACKUP_DIR=/data/adb/chargecontrol-backup
NEW_DIR=/data/adb/chargecontrol-new
DATA_DIR=/data/adb/battery-manager
LOG=$DATA_DIR/hot-update.log

mkdir -p "$DATA_DIR" 2>/dev/null
log() { echo "$(date '+%Y-%m-%d %H:%M:%S') $*" >> "$LOG" 2>/dev/null; }

# ══════════════════════════════════════════════════════════
# v2.1.1 快速进程查找
#
# 原来用 `for _p in /proc/[0-9]*; do tr '\0' ' ' < cmdline` 遍历，
# 在 Android 上单次扫描 300+ 进程要 300-500ms，套上循环和 sleep
# 后整个热更新拖到 100 秒。
#
# 改用 pgrep（C 实现，毫秒级）。找不到时返回空。
# ══════════════════════════════════════════════════════════
find_pids() {
  # $1 = 精确进程名（battd / supervisor.sh ...）
  # 用 pgrep -x（精确匹配进程名，C 实现，毫秒级）
  #   - 不用 pgrep -f：会匹配到调用者自己（cmdline 含该串）
  #   - 不用 /proc 遍历：Android 上 300+ 进程要 300-500ms
  _kw="$1"
  _self=$$
  _out=""
  # 先试 pgrep -x（精确名）
  for _pid in $(pgrep -x "$_kw" 2>/dev/null); do
    [ "$_pid" = "$_self" ] && continue
    _out="$_out $_pid"
  done
  # 名字含 . 的（supervisor.sh）pgrep -x 拿不到，用 ps 兜底
  case "$_kw" in
    *.*)
      for _pid in $(ps -A -o PID,CMD 2>/dev/null | grep -F "$_kw" | grep -v grep | awk '{print $1}'); do
        [ "$_pid" = "$_self" ] && continue
        _out="$_out $_pid"
      done
      ;;
  esac
  echo "$_out" | tr ' ' '\n' | grep -v '^$' | sort -u
}

# 快速判断某进程是否还活着（校验 cmdline 防 PID 复用）
pid_alive_matching() {
  # $1 = pid, $2 = 关键词
  [ -n "$1" ] && [ -d "/proc/$1" ] || return 1
  # /proc/PID/cmdline 存在且含关键词
  case "$(tr '\0' ' ' < "/proc/$1/cmdline" 2>/dev/null)" in
    *"$2"*) return 0 ;;
    *) return 1 ;;
  esac
}

# 等某类进程全部退出（用 pgrep，最多 N 秒）
wait_pids_gone() {
  # $1 = 正则模式, $2 = 超时秒数
  _pat="$1"; _to="${2:-15}"; _n=0
  while [ $_n -lt "$_to" ]; do
    if [ -z "$(find_pids "$_pat")" ]; then return 0; fi
    sleep 0.5
    _n=$((_n+1))
  done
  return 1
}


# ── 0. 参数校验 ──
if [ -z "$SRC" ] || [ ! -d "$SRC" ]; then
  log "FATAL: 无效的源目录 '$SRC'"
  exit 1
fi
if [ ! -f "$SRC/module.prop" ] || [ ! -f "$SRC/bin/battd" ]; then
  log "FATAL: 源目录结构不完整（缺 module.prop 或 bin/battd）"
  exit 1
fi

# ═══ v2.1.3: 清理历史遗留的重复 ID 模块目录 ═══
#   老版本 update.sh 的备份是 $MOD_DIR.old-run / .pre-hot-update，
#   都在 /data/adb/modules/ 下，带 module.prop，会撞管理器 key。
_STALE_DIRS=""
for _d in /data/adb/modules/chargecontrol*; do
  [ -d "$_d" ] || continue
  [ "$_d" = "/data/adb/modules/chargecontrol" ] && continue
  _STALE_DIRS="$_STALE_DIRS $_d"
done
if [ -n "$_STALE_DIRS" ]; then
  for _d in $_STALE_DIRS; do
    rm -rf "$_d" 2>/dev/null
    log "清理遗留备份目录: $_d"
  done
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
while [ $_w -lt 10 ]; do
  _found="$(ps -A -o CMD 2>/dev/null | grep -E -- "--install-module|module install|apd install|pm install" | grep -v grep | head -1)"
  [ -z "$_found" ] && break
  sleep 0.5
  _w=$((_w+1))
done
[ $_w -gt 0 ] && log "  额外等了 $((_w/2))s 等安装器退出"

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
# supervisor.sh 是脚本，pgrep -x 拿不到；用 pidfile（它自己写的）
_sup_pid="$(cat "$DATA_DIR/supervisor.pid" 2>/dev/null)"
if [ -n "$_sup_pid" ] && [ -d "/proc/$_sup_pid" ]; then
  kill -TERM "$_sup_pid" 2>/dev/null
  log "  supervisor pid=$_sup_pid → TERM"
else
  # 兜底：ps 找（比 /proc 遍历快）
  _sup_pid="$(ps -A -o PID,CMD 2>/dev/null | grep -F "supervisor.sh" | grep -v grep | awk '{print $1}' | head -1)"
  if [ -n "$_sup_pid" ]; then
    kill -TERM "$_sup_pid" 2>/dev/null
    log "  supervisor pid=$_sup_pid → TERM (by ps)"
  else
    log "  supervisor 未在运行"
  fi
fi
sleep 0.5

# ── 3. 停 battd ──
log "停止 battd"
_old_pids="$(find_pids "battd")"
if [ -n "$_old_pids" ]; then
  for _pid in $_old_pids; do
    kill -TERM "$_pid" 2>/dev/null
  done
  log "  battd 旧进程: $_old_pids"
else
  log "  battd 旧进程: 无"
fi

# 等退出（最多 15 秒，比原来 60s 快 4 倍）
if ! wait_pids_gone "battd" 15; then
  _still="$(find_pids "battd")"
  for _pid in $_still; do
    kill -KILL "$_pid" 2>/dev/null
    log "  强制 KILL 残留 pid=$_pid"
  done
  sleep 0.5
fi

# ── 4. 替换模块目录（旧目录先备份）──
log "备份旧模块目录"
# v2.1.3: 备份到 modules 之外（/data/adb/chargecontrol-backup）
#          避免带 module.prop 的副本撞管理器的 LazyColumn key
rm -rf "$BACKUP_DIR" 2>/dev/null
if [ -d "$MOD_DIR" ]; then
  if mv "$MOD_DIR" "$BACKUP_DIR" 2>/dev/null; then
    log "  旧目录已移走 → $BACKUP_DIR（mv，毫秒级）"
  else
    cp -a "$MOD_DIR" "$BACKUP_DIR" 2>/dev/null && log "  旧目录已复制（fallback）"
  fi
fi

log "替换模块目录"
# ⚠️ v2.0.1 修复：这里原来有一行 `rm -f "$SRC/update.sh"`，
# 本意是「不要把 update.sh 带进目标」，但删的是**解压目录**里的文件，
# 导致 cp -a 后模块目录缺少 update.sh ⇒ 下次热更新失效。
# update.sh 是热更新的执行者，应该被复制进模块目录。
rm -rf "$NEW_DIR" 2>/dev/null
if ! cp -a "$SRC" "$NEW_DIR" 2>>"$LOG"; then
  log "FATAL: 复制失败，旧目录保持不动"
  rm -rf "$NEW_DIR" 2>/dev/null
  # 恢复旧目录
  [ -d "$BACKUP_DIR" ] && mv "$BACKUP_DIR" "$MOD_DIR"
  exit 1
fi
if ! mv "$NEW_DIR" "$MOD_DIR" 2>>"$LOG"; then
  log "FATAL: 重命名失败，回滚"
  rm -rf "$NEW_DIR" 2>/dev/null
  [ -d "$BACKUP_DIR" ] && mv "$BACKUP_DIR" "$MOD_DIR"
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
fi

# v2.1.2: 轮询等 battd 起来（起来就返回，不等固定时长）
_n=0
while [ $_n -lt 20 ]; do
  if [ -n "$(find_pids battd)" ]; then break; fi
  # 1 秒后还没起来，直接启动（不等 supervisor）
  if [ $_n -eq 1 ] && [ -z "$(find_pids battd)" ]; then
    log "  supervisor 未拉起，直接启动 battd"
    setsid "$DATA_DIR/battd" >> "$DATA_DIR/battd.stdout" 2>&1 &
  fi
  sleep 0.5
  _n=$((_n+1))
done

# ── 7. 验证 ──
_new_pid="$(find_pids "battd" | head -1)"

if [ -n "$_new_pid" ]; then
  log "SUCCESS: 新 battd pid=$_new_pid, version=$NEW_VER"
  rm -rf "$BACKUP_DIR" "$NEW_DIR" 2>/dev/null   # 成功就清备份
  log "=== 热更新完成 ==="
else
  log "FAIL: 新进程未启动，回滚"
  if [ -d "$BACKUP_DIR" ]; then
    rm -rf "$MOD_DIR" && mv "$BACKUP_DIR" "$MOD_DIR"
    cp -f "$MOD_DIR/bin/battd" "$DATA_DIR/battd" 2>/dev/null
    nohup "$DATA_DIR/battd" >> "$DATA_DIR/battd.stdout" 2>&1 < /dev/null &
    log "  已回滚"
  fi
  exit 1
fi
exit 0
