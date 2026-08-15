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

ui_print "- 安装完成，请重启"
