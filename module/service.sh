#!/system/bin/sh
MODDIR=${0%/*}

# 等待系统启动完成
while [ "$(getprop sys.boot_completed)" != "1" ]; do
  sleep 2
done

# 启动 battd 守护进程
/data/adb/battery-manager/battd &
