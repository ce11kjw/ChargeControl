#!/bin/bash
# ChargeControl 冒烟测试 —— 在假 sysfs 上跑 battd 并校验所有端点
# 由 .github/workflows/build-module.yml 的 test-x86 job 调用
set -e

FS=/tmp/ccfs
rm -rf "$FS"
mkdir -p "$FS"/sys/class/power_supply/battery \
         "$FS"/sys/class/power_supply/usb \
         "$FS"/data/adb/battery-manager

echo "── 造假电池节点（值取自真机 K60 Ultra）──"
echo 78      > "$FS"/sys/class/power_supply/battery/capacity
echo Charging > "$FS"/sys/class/power_supply/battery/status
echo 4200    > "$FS"/sys/class/power_supply/battery/voltage_now
echo 1500    > "$FS"/sys/class/power_supply/battery/current_now
echo 380     > "$FS"/sys/class/power_supply/battery/temp
echo 26      > "$FS"/sys/class/power_supply/battery/cycle_count
echo 5413000 > "$FS"/sys/class/power_supply/battery/charge_full
echo 5380000 > "$FS"/sys/class/power_supply/battery/charge_full_design

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
