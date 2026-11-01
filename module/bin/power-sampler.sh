#!/system/bin/sh
# ChargeControl · 采样器（60 s 一轮）
#   1) 硬件耗电分布 → webroot/power.json   （batterystats 估算）
#   2) 搬运学习曲线 / 累计统计 → webroot/   （前端直接 fetch）
#   3) 每 10 轮生成一次电流基线 → webroot/baseline.json（异常诊断用）

DIR=/data/adb/battery-manager
WEB=$DIR/webroot
OUT=$WEB/power.json
TMP=$OUT.tmp
LED=/sys/class/leds/lcd-backlight
TICK=$DIR/.sampler_tick
NOW=$(date +%s)

mkdir -p "$WEB"

# ── 1) 硬件耗电 ──
RAW=$(dumpsys batterystats --charged 2>/dev/null | awk '
  /Estimated power use/ {f=1; next}
  f && (/\(on battery/ || /^[[:space:]]+UID/ || /^[[:space:]]*$/) {exit}
  f {print}
')
CAP=$(echo "$RAW"   | awk -F'[: ,]+' '/Capacity:/       {print $3; exit}')
DRAIN=$(echo "$RAW" | awk -F'[: ,]+' '/Computed drain:/  {print $6; exit}')
ITEMS=$(echo "$RAW" | awk -F: '
  NF>=2 {
    name=$1; gsub(/[[:space:]]/,"",name)
    if (name=="" || name=="Global" || name=="Capacity" || name=="Computed" || name=="actual") next
    split($2, a, " "); v=a[1]+0
    if (v>0) { if (n++) printf ","; printf "{\"k\":\"%s\",\"mah\":%s}", name, v }
  }')
BL_CUR=0; BL_MAX=0
[ -r "$LED/brightness" ] && BL_CUR=$(cat "$LED/brightness" 2>/dev/null)
[ -r "$LED/max_brightness" ] && BL_MAX=$(cat "$LED/max_brightness" 2>/dev/null)

printf '{"t":%s,"capacity_mah":%s,"drain_mah":%s,"backlight":{"cur":%s,"max":%s},"items":[%s]}\n' \
  "$NOW" "${CAP:-0}" "${DRAIN:-0}" "${BL_CUR:-0}" "${BL_MAX:-0}" "${ITEMS}" > "$TMP"
if [ -s "$TMP" ] && grep -q "items" "$TMP"; then
  mv -f "$TMP" "$OUT"; chmod 644 "$OUT"
else
  rm -f "$TMP"
fi

# ── 2) 搬运学习曲线 / 累计统计（battd 只 serve webroot）──
for f in charge_profile stats; do
  [ -f "$DIR/$f.json" ] || continue
  if [ "$DIR/$f.json" -nt "$WEB/$f.json" ] 2>/dev/null || [ ! -f "$WEB/$f.json" ]; then
    cp -f "$DIR/$f.json" "$WEB/$f.json" 2>/dev/null
    chmod 644 "$WEB/$f.json" 2>/dev/null
  fi
done

# ── 3) 电流基线：每 10 轮（约 10 分钟）重算一次 ──
N=0
[ -f "$TICK" ] && N=$(cat "$TICK" 2>/dev/null)
N=$((N + 1))
echo "$N" > "$TICK"

if [ $((N % 3)) -eq 1 ]; then
  # 按 (温度2°C × 电量10%) 分组，取历史最大充电电流作基线
  # history.json 是 JSONL，格式固定，用字段名匹配取值
  busybox awk '
    /"s":"Charging"/ {
      c=""; tmp=""; i=""
      n=split($0, a, ",")
      for (k=1;k<=n;k++) {
        if (a[k] ~ /"c":/)   { sub(/.*"c":/,"",a[k]);   c=a[k]+0 }
        if (a[k] ~ /"tmp":/) { sub(/.*"tmp":/,"",a[k]); tmp=a[k]+0 }
        if (a[k] ~ /"i":/)   { sub(/.*"i":/,"",a[k]);   i=a[k]+0; if (i<0) i=-i }
      }
      if (i<50 || tmp<=0 || c<=0) next
      tb=int(tmp/2)*2; cb=int(c/10)*10
      key=tb "_" cb
      cnt[key]++
      if (i>max[key]) max[key]=i
    }
    END {
      printf "{\"t\":%d,\"groups\":{", '"$NOW"'
      first=1
      for (k in max) {
        if (cnt[k] < 3) continue          # 样本太少不可信
        if (!first) printf ","
        first=0
        printf "\"%s\":{\"max\":%d,\"n\":%d}", k, max[k], cnt[k]
      }
      printf "}}\n"
    }
  ' "$DIR/history.json" > "$WEB/baseline.json.tmp" 2>/dev/null

  if [ -s "$WEB/baseline.json.tmp" ] && grep -q "groups" "$WEB/baseline.json.tmp"; then
    mv -f "$WEB/baseline.json.tmp" "$WEB/baseline.json"
    chmod 644 "$WEB/baseline.json"
  else
    rm -f "$WEB/baseline.json.tmp"
  fi
fi

# ── 4) 每日电池寿命快照（cyc/soh/满充容量），用于按实际使用速率推算 ──
CYC_FILE=$DIR/cycling.jsonl
TODAY=$(date +%Y-%m-%d)
LAST_D=$(tail -1 "$CYC_FILE" 2>/dev/null | sed -n 's/.*"d":"\([^"]*\)".*/\1/p')
if [ "$LAST_D" != "$TODAY" ]; then
  _C=$(cat /sys/class/power_supply/battery/cycle_count 2>/dev/null)
  _S=$(cat /sys/class/power_supply/bms/soh 2>/dev/null)
  _F=$(cat /sys/class/power_supply/battery/charge_full 2>/dev/null)
  _CAP=$(cat /sys/class/power_supply/battery/capacity 2>/dev/null)
  [ -n "$_F" ] && _F=$((_F / 1000))
  if [ -n "$_C" ]; then
    printf '{"d":"%s","t":%s,"cyc":%s,"soh":%s,"full":%s,"cap":%s}\n' \
      "$TODAY" "$NOW" "${_C:-0}" "${_S:--1}" "${_F:-0}" "${_CAP:-0}" >> "$CYC_FILE"
    _L=$(wc -l < "$CYC_FILE" 2>/dev/null)
    if [ "${_L:-0}" -gt 400 ]; then
      tail -300 "$CYC_FILE" > "$CYC_FILE.tmp" && mv -f "$CYC_FILE.tmp" "$CYC_FILE"
    fi
  fi
fi
if [ -f "$CYC_FILE" ]; then
  busybox awk 'BEGIN{printf "["} {if(n++)printf ","; printf "%s", $0} END{printf "]"}' \
    "$CYC_FILE" > "$WEB/cycling.json.tmp" 2>/dev/null
  [ -s "$WEB/cycling.json.tmp" ] && mv -f "$WEB/cycling.json.tmp" "$WEB/cycling.json" && chmod 644 "$WEB/cycling.json"
fi
