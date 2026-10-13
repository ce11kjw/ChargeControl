#!/system/bin/sh
DIR=/data/adb/battery-manager
[ -f "$DIR/supervisor.pid" ] && kill "$(cat $DIR/supervisor.pid)" 2>/dev/null
killall battd 2>/dev/null
pkill -f "$DIR/battd" 2>/dev/null
sleep 1
rm -rf "$DIR"
