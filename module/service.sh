#!/system/bin/sh
MODDIR=${0%/*}
DIR=/data/adb/battery-manager

while [ "$(getprop sys.boot_completed)" != "1" ]; do
  sleep 2
done

if [ ! -f "$DIR/battd" ]; then
  mkdir -p "$DIR"
  cp "$MODDIR/bin/battd" "$DIR/battd"
  chmod 755 "$DIR/battd"
  cp "$MODDIR/batt.conf" "$DIR/batt.conf"
  cp -r "$MODDIR/webroot/." "$DIR/webroot/"
fi

cp -f "$MODDIR/supervisor.sh" "$DIR/supervisor.sh" 2>/dev/null
chmod 755 "$DIR/supervisor.sh" 2>/dev/null

if [ -x "$DIR/supervisor.sh" ]; then
  "$DIR/supervisor.sh" start >> "$DIR/supervisor.log" 2>&1 &
else
  killall battd 2>/dev/null
  "$DIR/battd" &
fi
exit 0
