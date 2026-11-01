#!/bin/bash
# v2.2.1 审核回归：重点覆盖 time_to_empty 放电推算 + 状态切换清零
set -e
FS=/tmp/gt/fsr; rm -rf "$FS"
B="$FS/sys/class/power_supply"
mkdir -p $B/{battery,bms,usb,mtk-master-charger,11280000.i2c:mt6375@34:chg} \
         $FS/proc/mtk_battery_cmd $FS/proc/device-tree $FS/data/adb/battery-manager $FS/thermal
for i in $(seq 0 95); do mkdir -p $FS/thermal/thermal_zone$i; done
echo quiet_therm > $FS/thermal/thermal_zone42/type; echo 39300 > $FS/thermal/thermal_zone42/temp

# ══ 场景 A：放电（current_now 为正 ⇒ bat_curr 为负）══
mk_discharge() {
  echo 46 > $B/battery/capacity
  echo Discharging > $B/battery/status
  echo 3864000 > $B/battery/voltage_now
  echo 1138000 > $B/battery/current_now     # 放电：正 ⇒ bat_curr = -1138
  echo 380 > $B/battery/temp
}
# ══ 场景 B：充电（current_now 为负 ⇒ bat_curr 为正）══
mk_charge() {
  echo 60 > $B/battery/capacity
  echo Charging > $B/battery/status
  echo 4200000 > $B/battery/voltage_now
  echo -2000000 > $B/battery/current_now    # 充电：负 ⇒ bat_curr = +2000
  echo 370 > $B/battery/temp
}
mk_discharge
echo 5261000 > $B/battery/charge_full; echo 5380000 > $B/battery/charge_full_design
echo 4460 > $B/battery/constant_charge_voltage; echo 2271000 > $B/battery/charge_counter
echo 1 > $B/battery/present; echo Li-poly > $B/battery/technology
echo Good > $B/battery/health; echo Low > $B/battery/capacity_level
echo 57 > $B/battery/cycle_count
echo 99 > $B/bms/soh; echo 264 > $B/bms/temp_max; echo 254 > $B/bms/temp_min
echo 100000 > $B/bms/resistance; echo 0 > $B/bms/charge_done
echo 2271 > $B/bms/rm; echo 5261 > $B/bms/fcc; echo 4543 > $B/bms/voltage_max
echo BYD > $B/bms/battery_supplier
echo 430 > $B/mtk-master-charger/temp           # 0.1°C：430 = 43.0°C（修正：原测试写成 4300000）
echo 3000000 > $B/mtk-master-charger/constant_charge_current_max
echo 3000000 > $B/mtk-master-charger/input_current_limit
echo 100  > "$B/11280000.i2c:mt6375@34:chg/charge_term_current"
echo 4050 > "$B/11280000.i2c:mt6375@34:chg/calibrate"
echo 4500 > "$B/11280000.i2c:mt6375@34:chg/input_voltage_limit"
echo 0 > $B/usb/online; echo 0 > $B/usb/current_now; echo 0 > $B/usb/voltage_now
echo 4460 > $FS/proc/mtk_battery_cmd/set_cv; echo 0 > $FS/proc/mtk_battery_cmd/en_power_path
echo "0 0" > $FS/proc/mtk_battery_cmd/current_cmd; echo 1 > $FS/proc/mtk_battery_cmd/en_safety_timer
echo MT6985 > $FS/proc/device-tree/model

sed -e "s|\"/sys/class/power_supply/battery\"|\"$B/battery\"|" \
    -e "s|\"/sys/class/power_supply/usb\"|\"$B/usb\"|" \
    -e "s|\"/sys/class/power_supply/bms\"|\"$B/bms\"|" \
    -e "s|\"/sys/class/power_supply/mtk-master-charger\"|\"$B/mtk-master-charger\"|" \
    -e "s|\"/sys/class/power_supply/11280000.i2c:mt6375@34:chg\"|\"$B/11280000.i2c:mt6375@34:chg\"|" \
    -e "s|\"/sys/class/thermal\"|\"$FS/thermal\"|" \
    -e "s|/data/adb/battery-manager|$FS/data/adb/battery-manager|g" \
    -e "s|/proc/mtk_battery_cmd|$FS/proc/mtk_battery_cmd|g" \
    -e "s|/proc/device-tree|$FS/proc/device-tree|g" \
    -e "s|#define PORT        8800|#define PORT        18805|" \
    src/battd.c > /tmp/gt/tr.c
gcc -O2 -w -o /tmp/gt/tr /tmp/gt/tr.c || { echo BUILD_FAIL; exit 1; }

run_wait() { setsid /tmp/gt/tr >> /tmp/gt/tr.out 2>&1 </dev/null & sleep 40; }
stop_app() { pkill -x tr 2>/dev/null || true; sleep 1; }

echo "── 场景A：放电 ──"
run_wait
curl -s -m 5 http://127.0.0.1:18805/api/status > /tmp/gt/sa.json
stop_app
python3 - <<'PY'
import json
d=json.load(open('/tmp/gt/sa.json'))
rm=2271; cur=-1138
exp=int(rm*3600/1138)
print(f"  current      = {d.get('current')}  (期望 -1138)")
print(f"  time_to_empty= {d.get('time_to_empty')}  (期望 {exp} 秒 ≈ {exp/60:.0f} 分)")
print(f"  voltage_ocv  = {d.get('voltage_ocv')}  (放电大电流 ⇒ 应 -1)")
print(f"  charger_temp = {d.get('charger_temp')}  (期望 430 = 43.0°C)")
ok = (d.get('current')==-1138 and d.get('time_to_empty')==exp
      and d.get('voltage_ocv')==-1 and d.get('charger_temp')==430)
print("  " + ("✅ 场景A 通过" if ok else "❌ 场景A 失败"))
PY

echo "── 场景B：充电（验证 time_to_empty 清零）──"
mk_charge
run_wait
curl -s -m 5 http://127.0.0.1:18805/api/status > /tmp/gt/sb.json
stop_app
python3 - <<'PY'
import json
d=json.load(open('/tmp/gt/sb.json'))
print(f"  status       = {d.get('status')}")
print(f"  current      = {d.get('current')}  (期望 +2000)")
print(f"  time_to_empty= {d.get('time_to_empty')}  (充电 ⇒ 必须 -1，不能残留)")
ok = (d.get('status')=='Charging' and d.get('current')==2000 and d.get('time_to_empty')==-1)
print("  " + ("✅ 场景B 通过" if ok else "❌ 场景B 失败"))
PY
