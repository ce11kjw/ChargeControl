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

# 首次安装：铺运行目录
if [ ! -x "$DIR/battd" ]; then
  cp "$MODDIR/bin/battd" "$DIR/battd" 2>/dev/null
  chmod 755 "$DIR/battd" 2>/dev/null
  cp "$MODDIR/batt.conf" "$DIR/batt.conf" 2>/dev/null
  mkdir -p "$DIR/webroot"
  cp -r "$MODDIR/webroot/." "$DIR/webroot/" 2>/dev/null
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
