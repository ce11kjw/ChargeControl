#!/system/bin/sh

# 优先使用安装器注入的 $MODPATH，手动执行时回退到脚本路径
if [ -z "$MODPATH" ]; then
  MODPATH=${0%/*}
fi

if [ "$KSU" = "true" ]; then
  ui_print "- KernelSU 环境"
elif [ "$APATCH" = "true" ]; then
  ui_print "- APatch 环境"
else
  ui_print "- Magisk 环境"
fi

BAT=/sys/class/power_supply/battery
USB=/sys/class/power_supply/usb

# 节点检测（用 su 获取真 root 权限，覆盖 SELinux 限制）
R() { su -c "cat $1" 2>/dev/null | head -c 40; }
has() { [ -n "$(R $1)" ] || [ -e "$1" ]; }

ui_print "- 电池控制节点"
if has "$BAT/input_suspend"; then
  ui_print "  [Y] input_suspend = $(R $BAT/input_suspend) (可控制充电)"
else
  ui_print "  [N] input_suspend (无此节点)"
fi
if has "$BAT/charge_control_limit"; then
  ui_print "  [Y] charge_control_limit = $(R $BAT/charge_control_limit) (备选节点)"
fi

ui_print "- 电池监控节点"
for f in capacity temp voltage_now current_now status; do
  v=$(R $BAT/$f)
  if [ -n "$v" ]; then ui_print "  [Y] $f = $v"; else ui_print "  [N] $f"; fi
done

ui_print "- 电池健康节点"
for f in cycle_count charge_full charge_full_design; do
  v=$(R $BAT/$f)
  if [ -n "$v" ]; then ui_print "  [Y] $f = $v"; else ui_print "  [N] $f"; fi
done

ui_print "- 充电协议节点"
for f in online real_type type pd_type power_max; do
  v=$(R $USB/$f)
  if [ -n "$v" ]; then ui_print "  [Y] $f = $v"; else ui_print "  [N] $f"; fi
done

ui_print "- 硬件温度 (热区摘要)"
Z=0
found=0
while [ $Z -lt 82 ]; do
  t=$(R /sys/class/thermal/thermal_zone$Z/type)
  if [ -n "$t" ]; then
    case "$t" in
      soc_max|gpu1|mtk-master-charger|X7_therm|battery)
        ui_print "  [Y] zone$Z $t = $(R /sys/class/thermal/thermal_zone$Z/temp)"
        found=1 ;;
    esac
  fi
  Z=$((Z+1))
done
if [ $found -eq 0 ]; then ui_print "  [N] 未找到关键热区"; fi

# 释放二进制
mkdir -p /data/adb/battery-manager
cp "$MODPATH/bin/battd" /data/adb/battery-manager/battd
chmod 755 /data/adb/battery-manager/battd

# 复制配置
cp "$MODPATH/batt.conf" /data/adb/battery-manager/batt.conf

# 复制 webui
mkdir -p /data/adb/battery-manager/webroot
cp -r "$MODPATH/webroot/." /data/adb/battery-manager/webroot/

# 复制 supervisor（保证 battd 常驻）
cp "$MODPATH/supervisor.sh" /data/adb/battery-manager/supervisor.sh 2>/dev/null
chmod 755 /data/adb/battery-manager/supervisor.sh 2>/dev/null

ui_print "- MTK 扩展节点探测"
for f in charge_control_limit night_charging smart_chg; do
  v=$(R $BAT/$f)
  if [ -n "$v" ]; then ui_print "  [Y] $f = $v"; else ui_print "  [N] $f"; fi
done
if [ -e /proc/mtk_battery_cmd/en_power_path ]; then
  ui_print "  [Y] mtk_battery_cmd/en_power_path = $(R /proc/mtk_battery_cmd/en_power_path) (旁路电源路径)"
fi
if [ -e /sys/class/power_supply/bms/soh ]; then
  ui_print "  [Y] bms/soh = $(R /sys/class/power_supply/bms/soh) (原生健康度)"
fi

# ═══ 安装伴随的 App（模块 zip 里带了 APK）═══
ui_print "- 安装 App"
APK=""
for cand in "$MODPATH/ChargeControl.apk" "$MODPATH/app/ChargeControl.apk"; do
  [ -f "$cand" ] && { APK="$cand"; break; }
done

if [ -n "$APK" ]; then
  VER=$(su -c "dumpsys package com.ce11kjw.chargecontrol 2>/dev/null | grep -m1 versionName" 2>/dev/null | tr -d ' ')
  if [ -n "$VER" ]; then
    ui_print "  已安装: $VER，尝试覆盖升级"
  fi
  # 优先 pm install（无需用户操作）
  OUT=$(su -c "pm install -r -d '$APK'" 2>&1)
  case "$OUT" in
    *Success*)
      ui_print "  [Y] App 安装成功"
      ;;
    *INSTALL_FAILED_UPDATE_INCOMPATIBLE*)
      ui_print "  [!] 签名不匹配（旧版签名不同），需手动卸载旧 App"
      ui_print "      已复制到 /sdcard/下载/ChargeControl.apk"
      cp -f "$APK" /sdcard/下载/ChargeControl.apk 2>/dev/null
      ;;
    *INSTALL_FAILED_VERSION_DOWNGRADE*)
      ui_print "  [!] 已装版本更新，跳过"
      ;;
    *)
      ui_print "  [!] 自动安装失败，已复制到 /sdcard/下载/ChargeControl.apk"
      ui_print "      $OUT"
      cp -f "$APK" /sdcard/下载/ChargeControl.apk 2>/dev/null
      ;;
  esac
else
  ui_print "  [-] zip 内未找到 APK"
fi

ui_print "- 安装完成"
ui_print "  模块已就绪，无需重启（battd 会自动启动）"
ui_print "  App 图标: ChargeControl"
