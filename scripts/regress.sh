#!/system/bin/sh
# ChargeControl 回归测试（M1-D）
# 用法：sh regress.sh        在手机上以 root 运行
# 能自动测的在这里；需人眼的项目见 docs/regression.md

BB=/data/adb/ap/bin/busybox
API=http://127.0.0.1:8800
D=/data/adb/battery-manager
W=$D/webroot
PASS=0; FAIL=0; WARN=0

ok(){ echo "  [PASS] $1"; PASS=$((PASS+1)); }
no(){ echo "  [FAIL] $1"; FAIL=$((FAIL+1)); }
warn(){ echo "  [WARN] $1"; WARN=$((WARN+1)); }

echo "======== ChargeControl 回归测试 ========"
echo "时间: $(date '+%Y-%m-%d %H:%M:%S')"
echo

echo "-- 1. 进程 --"
[ -n "$(pgrep -x battd)" ] && ok "battd 在跑 (pid $(pgrep -x battd))" || no "battd 没在跑"
if [ -n "$(pgrep -f 'supervisor')" ]; then ok "supervisor 在跑"; else warn "supervisor 没跑（battd 可能裸跑）"; fi

echo "-- 2. API --"
T0=$(date +%s%N)
S=$($BB wget -q -O - $API/api/status 2>/dev/null)
T1=$(date +%s%N)
MS=$(( (T1 - T0) / 1000000 ))
if [ -n "$S" ]; then ok "status 有响应"; else no "status 无响应"; fi
if [ "$MS" -lt 500 ]; then ok "status 响应 ${MS}ms"; else warn "status 偏慢 ${MS}ms"; fi

echo "-- 3. 页面 --"
for p in / /taste.html /index-classic.html; do
  if $BB wget -q -O /dev/null "$API$p" 2>/dev/null; then ok "页面 $p 200"; else no "页面 $p 打不开"; fi
done

echo "-- 4. 数据文件 --"
for f in power.json charge_profile.json stats.json baseline.json cycling.json; do
  if [ -s "$W/$f" ]; then ok "$f ($(stat -c %s $W/$f) 字节)"; else no "$f 缺失或为空"; fi
done

echo "-- 5. 关键字段 --"
for k in nodes tp_enabled tp_ratio charge_limit est_cycles_left gpu_load charge_full_mah health nodes; do
  case "$S" in
    *"\"$k\""*) ok "字段 $k 存在" ;;
    *) no "字段 $k 缺失" ;;
  esac
done

echo "-- 6. 字段合理性 --"
case "$S" in
  *'"gpu_load":-1'*) warn "gpu_load 仍是 -1（GED 未读到）" ;;
  *) ok "gpu_load 有值" ;;
esac
case "$S" in
  *'"est_cycles_left":0'*) warn "est_cycles_left=0（可能未初始化）" ;;
  *) ok "est_cycles_left 非 0" ;;
esac
case "$S" in
  *'"input_voltage_settled":-1'*) ok "input_voltage 未插电时正确为 -1" ;;
  *'"input_voltage_settled":6'*) no "input_voltage 仍是噪声值 6" ;;
  *) ok "input_voltage 有值" ;;
esac

echo "-- 7. 采样循环 --"
FOUND=0
for p in /proc/[0-9]*; do
  c=$(tr '\0' ' ' < "$p/cmdline" 2>/dev/null)
  case "$c" in *"power-sampler"*"sleep"*) FOUND=1 ;; esac
done
if [ "$FOUND" -eq 1 ]; then ok "采样循环在跑"; else no "采样循环没在跑（重启后不会自启就危险）"; fi

echo "-- 8. 资源 --"
RSS=$(ps -A -o RSS,NAME 2>/dev/null | awk '/battd/{print $1; exit}')
if [ -n "$RSS" ]; then
  if [ "$RSS" -lt 8192 ]; then ok "battd RSS ${RSS}KB"; else warn "battd RSS ${RSS}KB 偏大"; fi
else warn "读不到 battd RSS"; fi
AVAIL=$(df -k /data 2>/dev/null | awk 'NR==2{print int($4/1024)}')
if [ -n "$AVAIL" ]; then ok "磁盘可用 ${AVAIL}MB"; fi

echo "-- 9. 配置一致性 --"
if grep -q "^charge_limit=" "$D/batt.conf" 2>/dev/null; then
  CFG=$(sed -n 's/^charge_limit=//p' "$D/batt.conf" | head -1)
  API_V=$(echo "$S" | sed -n 's/.*"charge_limit":\([0-9]*\).*/\1/p')
  if [ "$CFG" = "$API_V" ]; then ok "charge_limit 配置($CFG)=内存($API_V)"; else warn "charge_limit 配置($CFG)≠内存($API_V)"; fi
else
  warn "batt.conf 里没有 charge_limit"
fi

echo
echo "======================================"
echo "结果: PASS=$PASS  FAIL=$FAIL  WARN=$WARN"
if [ "$FAIL" -eq 0 ]; then echo "✅ 无失败项"; else echo "❌ 有 $FAIL 项失败，需处理"; fi
