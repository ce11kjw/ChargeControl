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

# 运行目录由 update.sh 负责同步（v1.19.4）
# 这里只保证目录存在，避免后续 ui_print 检测节点时出错
mkdir -p /data/adb/battery-manager/webroot

# 给 update.sh 设权限（必须可执行）
[ -f "$MODPATH/update.sh" ] && chmod 755 "$MODPATH/update.sh" 2>/dev/null


# ═══════════════════════════════════════════════════════════
# ★ v1.19.4 热更新：交给独立的 update.sh 在后台执行
#
# 参考 nyehueh/File Manager v7.3.1 的做法：
#   customize.sh 只负责「解压 + 设权限 + 装 APK」，
#   真正的「替换模块目录 + 重启进程」交给 update.sh 后台做。
#
# 为什么不能在这里直接做：
#   · 此刻管理器还在操作 $MODPATH（解压临时目录），后面可能还会写文件
#   · 安装器进程未退出时替换 /data/adb/modules/chargecontrol 有竞态
#   · 此时杀 battd 也可能被 supervisor 立刻拉回旧的
#
# update.sh 会先等父进程（安装器）退出，再执行替换，最干净。
# ═══════════════════════════════════════════════════════════
ui_print "- 准备热更新"

UPDATE_SH=""
if [ -f "$MODPATH/update.sh" ]; then
  UPDATE_SH="$MODPATH/update.sh"
fi

if [ -z "$UPDATE_SH" ]; then
  ui_print "  [!] zip 内缺少 update.sh，回退到直接复制模式"
  # 兜底：老方式（直接复制，不杀进程）
  mkdir -p /data/adb/battery-manager/webroot
  cp -f "$MODPATH/bin/battd" /data/adb/battery-manager/battd 2>/dev/null
  chmod 755 /data/adb/battery-manager/battd 2>/dev/null
  cp -f "$MODPATH/batt.conf" /data/adb/battery-manager/batt.conf 2>/dev/null
  cp -rf "$MODPATH/webroot/." /data/adb/battery-manager/webroot/ 2>/dev/null
  cp -f "$MODPATH/supervisor.sh" /data/adb/battery-manager/supervisor.sh 2>/dev/null
  ui_print "  [!] 已复制文件，但需重启或手动重启服务才能生效"
else
  chmod 755 "$UPDATE_SH" 2>/dev/null

  # 先铺一份运行目录（update.sh 会再做一次，这里保证「装完立刻能用」）
  mkdir -p /data/adb/battery-manager/webroot 2>/dev/null
  [ -f "/data/adb/battery-manager/batt.conf" ] || \
    cp -f "$MODPATH/batt.conf" /data/adb/battery-manager/batt.conf 2>/dev/null

  # ★ 关键：后台拉起 update.sh，把解压目录传进去
  #   nohup + & 让它脱离安装器进程，安装器退出后继续跑
  nohup sh "$UPDATE_SH" "$MODPATH" >/dev/null 2>&1 &

  ui_print "  热更新已排队，安装完成后自动执行"
  ui_print "  （约 10-20 秒生效，无需重启手机）"
fi

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
# ═══════════════════════════════════════════════════════════
# v2.0.0: App 已移除（改用 PWA）
#   用户决策：HDR 夜光/悬浮窗不要，WebUI 改 PWA 后 App 无必要
#   如需 App 请从旧版本 Release 下载
# ═══════════════════════════════════════════════════════════

ui_print "- 安装完成"
ui_print "  热更新在后台运行，约 10-20 秒生效，无需重启手机"
ui_print "  可稍后刷新模块列表查看状态"
if [ -f "$PERSIST_APK" ]; then
  ui_print "  App 已就位，后续可用 App 内「模块更新」升级"
fi
