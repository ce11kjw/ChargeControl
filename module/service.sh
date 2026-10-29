#!/system/bin/sh
# ChargeControl 开机启动
#
# v1.10.0 修复：
#   · 开机先清理失效的 PID 文件（上次异常关机残留）
#   · supervisor 用 setsid 脱离会话启动，避免 boot 脚本退出时被一起回收
#   · 增加就绪等待与失败兜底

MODDIR=${0%/*}
DIR=/data/adb/battery-manager

# 等系统启动完成（最多 120 秒）
_i=0
while [ "$(getprop sys.boot_completed)" != "1" ] && [ $_i -lt 60 ]; do
  sleep 2
  _i=$((_i+1))
done

mkdir -p "$DIR"

# ★ v1.19.1 修复：按 sha256 同步，而不是「文件不存在才复制」
#
# 原实现 `if [ ! -x "$DIR/battd" ]` 只在首次安装时复制。
# 模块升级后 $DIR/battd 已存在（旧版），永远不替换 →
# 用户装了新模块，实际跑的仍是旧二进制。
# 实测踩过：模块 bin/battd 是 796024 字节(10-28)，
# 数据目录还是 730424 字节(10-17)，进程跑旧的。
#
# 现在：每次都对比 sha256，不一致就覆盖（幂等，不会无谓拷贝）。
_sha() { [ -f "$1" ] && sha256sum "$1" 2>/dev/null | cut -d' ' -f1 || echo ""; }

SRC_BIN="$MODDIR/bin/battd"
DST_BIN="$DIR/battd"
if [ -f "$SRC_BIN" ]; then
  S1=$(_sha "$SRC_BIN")
  S2=$(_sha "$DST_BIN")
  if [ "$S1" != "$S2" ]; then
    echo "$(date '+%Y-%m-%d %H:%M:%S') 同步 battd: ${S2:-无} → $S1" >> "$DIR/battd.stdout"
    cp -f "$SRC_BIN" "$DST_BIN" 2>/dev/null
    chmod 755 "$DST_BIN" 2>/dev/null
  fi
fi

# batt.conf：只在目标不存在时复制（保留用户配置）
[ -f "$DIR/batt.conf" ] || cp "$MODDIR/batt.conf" "$DIR/batt.conf" 2>/dev/null

# webroot：逐个文件对比，避免旧版残留
if [ -d "$MODDIR/webroot" ]; then
  mkdir -p "$DIR/webroot"
  for f in "$MODDIR"/webroot/*; do
    [ -f "$f" ] || continue
    b=$(basename "$f")
    a=$(_sha "$f")
    c=$(_sha "$DIR/webroot/$b")
    if [ "$a" != "$c" ]; then
      cp -f "$f" "$DIR/webroot/$b" 2>/dev/null
      echo "$(date '+%Y-%m-%d %H:%M:%S') 同步 webroot/$b" >> "$DIR/battd.stdout"
    fi
  done
fi

# 每次开机都同步最新脚本
cp -f "$MODDIR/supervisor.sh" "$DIR/supervisor.sh" 2>/dev/null
chmod 755 "$DIR/supervisor.sh" 2>/dev/null

# ★ 清理上次残留的 PID 文件（PID 会被复用，留着会导致误判）
rm -f "$DIR/battd.pid" "$DIR/supervisor.pid"

# 确保旧的 battd 已退出（模块被覆盖更新时旧进程会占着文件）
pkill -f "$DIR/battd" 2>/dev/null
sleep 2

if [ -x "$DIR/supervisor.sh" ]; then
  # ★ setsid 让 supervisor 脱离当前会话，boot 脚本退出后继续存活
  setsid "$DIR/supervisor.sh" start >> "$DIR/supervisor.log" 2>&1 &
  sleep 3
  # 兜底：若 supervisor 没起来，直接裸跑 battd
  if ! pgrep -f "supervisor.sh" >/dev/null 2>&1; then
    echo "$(date '+%Y-%m-%d %H:%M:%S') supervisor 未启动，回退裸跑 battd" >> "$DIR/supervisor.log"
    setsid "$DIR/battd" >> "$DIR/battd.stdout" 2>&1 &
  fi
else
  setsid "$DIR/battd" >> "$DIR/battd.stdout" 2>&1 &
fi

exit 0
