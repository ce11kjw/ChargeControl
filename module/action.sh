#!/system/bin/sh
# ChargeControl action.sh —— 模块管理器「操作」按钮：状态摘要
DIR=/data/adb/battery-manager
echo "════ ChargeControl v1.3.0 ════"
if [ -f "$DIR/battd.pid" ] && [ -d "/proc/$(cat $DIR/battd.pid 2>/dev/null)" ]; then
  echo "守护进程: 运行中 (pid $(cat $DIR/battd.pid))"
else
  echo "守护进程: 已停止"
fi
if [ -f "$DIR/supervisor.pid" ] && [ -d "/proc/$(cat $DIR/supervisor.pid 2>/dev/null)" ]; then
  echo "supervisor: 运行中"
fi
CURL="$(command -v curl 2>/dev/null)"
if [ -n "$CURL" ]; then
  echo "── 当前状态 ──"
  $CURL -s --max-time 3 http://127.0.0.1:8800/api/status | sed -n \
    's/.*"capacity":\([0-9-]*\).*/电量: \1%/p;s/.*"temp":\([0-9-]*\).*/电池温度: \1°C/p;s/.*"health":\([0-9-]*\).*/健康度: \1%/p;s/.*"chg_temp":\([0-9-]*\).*/充电IC: \1°C/p'
fi
echo "── 配置 ──"
cat "$DIR/batt.conf" 2>/dev/null
echo "WebUI: http://127.0.0.1:8800/"
exit 0
