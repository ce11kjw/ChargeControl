#!/system/bin/sh

# 停止守护进程
killall battd 2>/dev/null

# 删除数据目录
rm -rf /data/adb/battery-manager

