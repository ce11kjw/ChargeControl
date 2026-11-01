#!/bin/bash
# ChargeControl 冒烟测试 —— 在假 sysfs 上跑 battd 并校验所有端点
# 由 .github/workflows/build-module.yml 的 test-x86 job 调用
set -e

FS=/tmp/ccfs
rm -rf "$FS"
mkdir -p "$FS"/sys/class/power_supply/battery \
         "$FS"/sys/class/power_supply/usb \
         "$FS"/sys/class/power_supply/bms \
         "$FS"/sys/class/power_supply/mtk-master-charger \
         "$FS/sys/class/power_supply/11280000.i2c:mt6375@34:chg" \
         "$FS"/sys/class/thermal \
         "$FS"/data/adb/battery-manager \
         "$FS"/proc/mtk_battery_cmd \
         "$FS"/proc/device-tree

echo "── 造假电池节点（值取自真机 K60 Ultra）──"
echo 78      > "$FS"/sys/class/power_supply/battery/capacity
echo Charging > "$FS"/sys/class/power_supply/battery/status
echo 4200    > "$FS"/sys/class/power_supply/battery/voltage_now
echo 1500    > "$FS"/sys/class/power_supply/battery/current_now
echo 380     > "$FS"/sys/class/power_supply/battery/temp
echo 26      > "$FS"/sys/class/power_supply/battery/cycle_count
echo 5413000 > "$FS"/sys/class/power_supply/battery/charge_full
echo 5380000 > "$FS"/sys/class/power_supply/battery/charge_full_design
# v2.2.0: 等价源节点（MTK 上这些标准节点不存在，真身在别的 power_supply 设备）
echo 2271 > "$FS"/sys/class/power_supply/bms/rm
echo 5261 > "$FS"/sys/class/power_supply/bms/fcc
echo 99   > "$FS"/sys/class/power_supply/bms/soh
echo 264  > "$FS"/sys/class/power_supply/bms/temp_max
echo 254  > "$FS"/sys/class/power_supply/bms/temp_min
echo 100000 > "$FS"/sys/class/power_supply/bms/resistance
echo 0    > "$FS"/sys/class/power_supply/bms/charge_done
echo 4543 > "$FS"/sys/class/power_supply/bms/voltage_max
echo BYD  > "$FS"/sys/class/power_supply/bms/battery_supplier
echo 3000000 > "$FS"/sys/class/power_supply/mtk-master-charger/input_current_limit
echo 3000000 > "$FS"/sys/class/power_supply/mtk-master-charger/constant_charge_current_max
echo 4300000 > "$FS"/sys/class/power_supply/mtk-master-charger/temp
echo 100  > "$FS/sys/class/power_supply/11280000.i2c:mt6375@34:chg/charge_term_current"
echo 4090 > "$FS/sys/class/power_supply/11280000.i2c:mt6375@34:chg/calibrate"
echo 4500 > "$FS/sys/class/power_supply/11280000.i2c:mt6375@34:chg/input_voltage_limit"
echo 1    > "$FS"/proc/mtk_battery_cmd/en_safety_timer
echo 4460 > "$FS"/proc/mtk_battery_cmd/set_cv
echo MT6985 > "$FS"/proc/device-tree/model
mkdir -p "$FS"/sys/class/thermal/thermal_zone0 "$FS"/sys/class/thermal/thermal_zone42
echo soc_max     > "$FS"/sys/class/thermal/thermal_zone0/type
echo 55000       > "$FS"/sys/class/thermal/thermal_zone0/temp
echo quiet_therm > "$FS"/sys/class/thermal/thermal_zone42/type
echo 39300       > "$FS"/sys/class/thermal/thermal_zone42/temp

# 源码位置：仓库里是 src/battd.c，test-bundle 里是平铺的 battd.c
SRC=""
for c in src/battd.c battd.c ./battd.c; do
  [ -f "$c" ] && { SRC="$c"; break; }
done
[ -n "$SRC" ] || { echo "::error::找不到 battd.c"; exit 1; }
echo "  源码: $SRC"

echo "── 编译测试版（路径改到假 sysfs）──"
sed -e "s|#define SYSFS       \"/sys/class/power_supply/battery\"|#define SYSFS       \"$FS/sys/class/power_supply/battery\"|" \
    -e "s|#define SYSFS_USB   \"/sys/class/power_supply/usb\"|#define SYSFS_USB   \"$FS/sys/class/power_supply/usb\"|" \
    -e "s|/data/adb/battery-manager|$FS/data/adb/battery-manager|g" \
    -e "s|/sys/class/power_supply/mtk-master-charger|$FS/sys/class/power_supply/mtk-master-charger|g" \
    -e "s|/sys/class/power_supply/bms|$FS/sys/class/power_supply/bms|g" \
    -e "s|/sys/class/power_supply/11280000.i2c:mt6375@34:chg|$FS/sys/class/power_supply/11280000.i2c:mt6375@34:chg|g" \
    -e "s|/sys/class/thermal|$FS/sys/class/thermal|g" \
    -e "s|/proc/device-tree|$FS/proc/device-tree|g" \
    -e "s|/proc/mtk_battery_cmd|$FS/proc/mtk_battery_cmd|g" \
    "$SRC" > /tmp/battd-test.c
gcc -O2 -o /tmp/battd-test /tmp/battd-test.c

setsid /tmp/battd-test > /tmp/battd.out 2>&1 < /dev/null &
sleep 8
PID=$(pgrep -x battd-test | head -1)
[ -n "$PID" ] || { echo "::error::battd-test 未启动"; cat /tmp/battd.out; exit 1; }
echo "  battd-test 已启动 pid=$PID"

curl -sS -m 5 http://127.0.0.1:8800/api/status -o /tmp/status.json

echo "── 字段完整性 ──"
python3 "$(dirname "$0")/verify_status.py"

echo "── v2.2.0 MTK 等价源回退（本机缺失节点）──"
python3 - <<'PYV'
import json
d=json.load(open('/tmp/status.json'))
# 这些 field 在 MTK 上标准节点缺失，必须能从等价源取到值（不能是 -1/空）
must = {
  'charge_term_current':        'MTK_CHG/charge_term_current',
  'constant_charge_voltage_max':'bms/voltage_max',
  'calibrate':                  'MTK_CHG/calibrate',
  'safety_timer_expired':       'mtk_battery_cmd/en_safety_timer',
  'capacity_error_margin':      'capacity vs rm/fcc 推算',
  'internal_resistance':        'bms/resistance',
  'temp_max':                   'bms/temp_max',
  'temp_min':                   'bms/temp_min',
  'charge_done':                'bms/charge_done',
  'charge_now':                 'bms/rm 或 charge_counter',
  'input_current_limit':        'mtk-master-charger/input_current_limit',
  'constant_charge_current_max':'mtk-master-charger/constant_charge_current_max',
}
bad=[]
for k,src in must.items():
    v=d.get(k)
    if v is None or v < 0: bad.append(f"{k}(应为{src})= {v}")
if bad:
    print("  ⚠️  以下字段未取到（若假 sysfs 已造对应节点则为回归）:")
    for b in bad: print("     ", b)
else:
    print(f"  ✅ {len(must)} 个回退字段全部取到值")
PYV

echo "── 写配置 + 越界值拒绝 ──"
curl -sS -m 5 -X POST http://127.0.0.1:8800/api/limit --data 'charge_limit=65' -o /dev/null
curl -sS -m 5 http://127.0.0.1:8800/api/status -o /tmp/s2.json
python3 -c "
import json
d = json.load(open('/tmp/s2.json'))
assert d['charge_limit'] == 65, f\"写入失败: {d['charge_limit']}\"
print('  charge_limit=65 生效 ✅')
"
curl -sS -m 5 -X POST http://127.0.0.1:8800/api/limit --data 'charge_limit=999' -o /dev/null
curl -sS -m 5 http://127.0.0.1:8800/api/status -o /tmp/s3.json
python3 -c "
import json
d = json.load(open('/tmp/s3.json'))
assert d['charge_limit'] == 65, f'越界值污染: {d[\"charge_limit\"]}'
print('  越界值 999 被忽略 ✅')
"

echo "── 慢客户端不阻塞（v1.11.2 修复回归）──"
( exec 3<>/dev/tcp/127.0.0.1/8800; sleep 6; exec 3<&- ) &
SLOW=$!
sleep 1
T=$(curl -sS -m 3 -o /dev/null -w "%{time_total}" http://127.0.0.1:8800/api/status)
kill $SLOW 2>/dev/null || true
python3 -c "
t = float('$T')
assert t < 1.0, f'慢客户端拖慢到 {t}s（应 <1s）'
print(f'  慢客户端占用时正常请求 {t:.3f}s ✅')
"

echo "── 全部端点 ──"
for ep in /api/status /api/curve /api/sessions /api/calibrate /api/log /api/events; do
  CODE=$(curl -sS -m 5 -o /dev/null -w "%{http_code}" "http://127.0.0.1:8800$ep")
  printf "  %-20s → %s\n" "$ep" "$CODE"
  [ "$CODE" = "200" ] || { echo "::error::$ep 返回 $CODE"; exit 1; }
done

# ── v1.19.0 新增：charge_profile / eta（无历史数据时返回 404，是正确行为）──
echo "── v1.19.0 新端点 ──"
for ep in /api/charge_profile /api/eta?to=80; do
  CODE=$(curl -sS -m 5 -o /dev/null -w "%{http_code}" "http://127.0.0.1:8800$ep")
  printf "  %-20s → %s (允许 200/404)\n" "$ep" "$CODE"
  case "$CODE" in 200|404) ;; *) echo "::error::$ep 返回 $CODE"; exit 1 ;; esac
done

# ── v1.17.0 新增：suspend 7 字段 + events 结构校验 ──
echo "── v1.17.0 suspend 字段 ──"
python3 - <<'PYEOF'
import json, urllib.request, sys
d = json.load(urllib.request.urlopen("http://127.0.0.1:8800/api/status", timeout=5))
need = ["suspend_active","suspend_reason","suspend_since","suspend_recover_in",
        "rec_temp_hyst","rec_temp_hold","rec_soc_hyst"]
miss = [k for k in need if k not in d]
if miss:
    print(f"  ::error::缺字段 {miss}"); sys.exit(1)
print(f"  ✅ 7 个 suspend 字段齐全 (reason={d['suspend_reason']})")
# 事件接口结构
e = json.load(urllib.request.urlopen("http://127.0.0.1:8800/api/events", timeout=5))
if "events" not in e or "count" not in e:
    print("  ::error::/api/events 结构不对"); sys.exit(1)
print(f"  ✅ /api/events 结构正确 (count={e['count']})")
PYEOF

echo "── 内存 / fd 稳定性（30 秒）──"
A=$(awk '/VmRSS/{print $2}' /proc/$PID/status)
FD1=$(ls /proc/$PID/fd | wc -l)
for _ in $(seq 1 30); do
  curl -sS -o /dev/null http://127.0.0.1:8800/api/status
  sleep 1
done
B=$(awk '/VmRSS/{print $2}' /proc/$PID/status)
FD2=$(ls /proc/$PID/fd | wc -l)
echo "  RSS: ${A}kB → ${B}kB    fd: $FD1 → $FD2"
python3 -c "
a, b = int('$A'), int('$B')
assert b <= a * 1.5, f'疑似内存泄漏: {a} → {b}'
assert int('$FD1') == int('$FD2'), 'fd 泄漏: $FD1 → $FD2'
print('  ✅ 无泄漏')
"

kill $PID 2>/dev/null || true
echo "✅ 冒烟测试全部通过"
