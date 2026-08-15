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

# 检测 sysfs 节点
if [ -f /sys/class/power_supply/battery/input_suspend ]; then
  ui_print "- 支持 input_suspend 充电控制"
else
  ui_print "- 未检测到 input_suspend，可能无法控制充电"
fi

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
