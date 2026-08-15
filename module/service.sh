#!/system/bin/sh
MODDIR=${0%/*}

# 等待系统启动完成
while [ "$(getprop sys.boot_completed)" != "1" ]; do
  sleep 2
done

# 确保二进制存在
if [ ! -f /data/adb/battery-manager/battd ]; then
  mkdir -p /data/adb/battery-manager
  cp "$MODDIR/bin/battd" /data/adb/battery-manager/battd
  chmod 755 /data/adb/battery-manager/battd
  cp "$MODDIR/batt.conf" /data/adb/battery-manager/batt.conf
  cp -r "$MODDIR/webroot/." /data/adb/battery-manager/webroot/
fi

# 停止旧进程
killall battd 2>/dev/null

# 启动守护进程
/data/adb/battery-manager/battd &
