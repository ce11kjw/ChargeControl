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

# ═══════════════════════════════════════════════════════════
# ★ v1.19.3 免重启生效：杀掉旧 battd，让 supervisor 自动拉起新的
#
# 原理（Linux）：进程运行时用内存里的代码，磁盘文件被替换不影响运行中的进程。
#   所以只 cp 新文件是不够的，旧进程还在跑旧代码。
#
# supervisor.sh 的主循环：
#     while true; do battd_running || start_battd; sleep 15; done
#   ⇒ 杀掉旧 battd 后，15 秒内自动拉起**新的**（从磁盘读取）。
#
# 安全措施：
#   1. 只杀 cmdline 含 battery-manager/battd 的进程（防 PID 复用误杀）
#   2. 先 SIGTERM 让它优雅退出（写日志/保存 stats），2 秒后再 SIGKILL
#   3. 最多等 20 秒确认新进程起来
# ═══════════════════════════════════════════════════════════
ui_print "- 应用更新（无需重启）"

DIR=/data/adb/battery-manager
NEW_SHA=$(sha256sum "$DIR/battd" 2>/dev/null | cut -d' ' -f1)

# 找出正在跑的 battd（校验 cmdline，防误杀）
_old_pids=""
for _p in /proc/[0-9]*; do
  _pid=${_p#/proc/}
  [ -r "$_p/cmdline" ] || continue
  _cmd=$(tr '\0' ' ' < "$_p/cmdline" 2>/dev/null)
  case "$_cmd" in
    *battery-manager/battd*)
      _old_pids="$_old_pids $_pid"
      ;;
  esac
done

if [ -n "$_old_pids" ]; then
  ui_print "  停止旧进程:$_old_pids"
  # SIGTERM 优雅退出
  for _pid in $_old_pids; do
    kill -TERM "$_pid" 2>/dev/null
  done
  sleep 2
  # 仍在的强杀
  for _pid in $_old_pids; do
    if [ -d "/proc/$_pid" ]; then
      kill -KILL "$_pid" 2>/dev/null
    fi
  done
  rm -f "$DIR/battd.pid" 2>/dev/null

  # 等 supervisor 拉起来（主循环 15 秒一轮，最多等 20 秒）
  ui_print "  等待 supervisor 自动重启…"
  _w=0
  while [ $_w -lt 20 ]; do
    sleep 1
    _w=$((_w+1))
    _new_pid=""
    for _p in /proc/[0-9]*; do
      _pid=${_p#/proc/}
      [ -r "$_p/cmdline" ] || continue
      _cmd=$(tr '\0' ' ' < "$_p/cmdline" 2>/dev/null)
      case "$_cmd" in
        *battery-manager/battd*) _new_pid=$_pid; break ;;
      esac
    done
    [ -n "$_new_pid" ] && break
  done

  if [ -n "$_new_pid" ]; then
    ui_print "  [Y] 已重启，新 PID=$_new_pid（用时 ${_w}s）"
  else
    ui_print "  [!] 20 秒内未自动重启，尝试手动拉起"
    # 兜底：直接起（不走 supervisor，supervisor 会自己发现并接管）
    if [ -x "$DIR/battd" ]; then
      ( cd "$DIR" && nohup ./battd >> battd.stdout 2>&1 < /dev/null & ) 2>/dev/null
      sleep 2
    fi
    # 再不行就提示重启
    _still=""
    for _p in /proc/[0-9]*; do
      _pid=${_p#/proc/}
      [ -r "$_p/cmdline" ] || continue
      _cmd=$(tr '\0' ' ' < "$_p/cmdline" 2>/dev/null)
      case "$_cmd" in
        *battery-manager/battd*) _still=$_pid; break ;;
      esac
    done
    if [ -n "$_still" ]; then
      ui_print "  [Y] 手动拉起成功，PID=$_still"
    else
      ui_print "  [!] 仍未启动，请重启手机或检查模块状态"
    fi
  fi
else
  ui_print "  当前无运行实例（首次安装，开机后自动启动）"
fi

# 确认磁盘上的版本已更新
_cur_sha=$(sha256sum "$DIR/battd" 2>/dev/null | cut -d' ' -f1)
if [ "$_cur_sha" = "$NEW_SHA" ] && [ -n "$NEW_SHA" ]; then
  ui_print "  [Y] battd 已更新 (${NEW_SHA%"${NEW_SHA#???????}"}…)"
else
  ui_print "  [!] battd 校验不一致"
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
# 安装伴随的 App
#
# APK 由 CI 打进 zip（module/ChargeControl.apk）。
# 注意点（都是踩过的）：
#   · pm 不一定在 PATH → 显式用 /system/bin/pm
#   · $MODPATH 含有临时目录路径，pm 读不到某些 SELinux 上下文
#     → 先把 APK 复制到 /data/local/tmp（pm 一定能读）
#   · 首次安装可能因为 packages 服务没就绪而失败 → 重试 3 次
#   · 失败时把 APK 留在 /sdcard/下载/ 让用户手动装
# ═══════════════════════════════════════════════════════════
ui_print "- 安装 App"

PERSIST_APK=/data/adb/battery-manager/ChargeControl.apk
FALLBACK_DIR=/sdcard/下载

APK=""
for cand in "$MODPATH/ChargeControl.apk" "$MODPATH/app/ChargeControl.apk"; do
  [ -f "$cand" ] && { APK="$cand"; break; }
done

if [ -z "$APK" ]; then
  ui_print "  [-] zip 内未找到 APK，跳过"
else
  ui_print "  APK: $(basename "$APK") ($(du -k "$APK" 2>/dev/null | cut -f1) KB)"

  # 保存一份到持久目录 —— OTA 升级时会重新安装它
  mkdir -p /data/adb/battery-manager 2>/dev/null
  cp -f "$APK" "$PERSIST_APK" 2>/dev/null && chmod 644 "$PERSIST_APK" 2>/dev/null

  # 复制到 /data/local/tmp（pm 一定能读，避免 SELinux 上下文问题）
  TMP_APK=/data/local/tmp/ChargeControl-install.apk
  cp -f "$APK" "$TMP_APK" 2>/dev/null
  chmod 644 "$TMP_APK" 2>/dev/null

  # 查已装版本
  OLDVER=""
  if [ -x /system/bin/pm ]; then
    OLDVER=$(/system/bin/pm dump com.ce11kjw.chargecontrol 2>/dev/null \
             | grep -m1 versionName | sed 's/.*versionName=//')
  fi
  [ -n "$OLDVER" ] && ui_print "  已安装: $OLDVER，尝试覆盖升级"

  # 安装（重试 3 次，应对 packages 服务未就绪）
  OK=0
  OUT=""
  i=1
  while [ $i -le 3 ]; do
    if [ -x /system/bin/pm ]; then
      OUT=$(/system/bin/pm install -r -d -g "$TMP_APK" 2>&1)
    else
      OUT=$(pm install -r -d -g "$TMP_APK" 2>&1)
    fi
    case "$OUT" in
      *Success*) OK=1; break ;;
    esac
    ui_print "  第 $i 次安装失败，2 秒后重试…"
    sleep 2
    i=$((i+1))
  done

  if [ $OK -eq 1 ]; then
    ui_print "  [Y] App 安装成功"
  else
    case "$OUT" in
      *INSTALL_FAILED_UPDATE_INCOMPATIBLE*|*signatures*do*not*match*)
        ui_print "  [!] 签名不匹配（旧版 App 签名不同）"
        ui_print "      请在系统设置里卸载旧版 ChargeControl 后重装模块"
        ;;
      *INSTALL_FAILED_VERSION_DOWNGRADE*)
        ui_print "  [!] 已装版本比 zip 内的新，跳过"
        OK=1
        ;;
      *INSTALL_FAILED_INSUFFICIENT_STORAGE*)
        ui_print "  [!] 存储空间不足"
        ;;
      *)
        ui_print "  [!] 自动安装失败"
        echo "$OUT" | head -3 | while read -r l; do ui_print "      $l"; done
        ;;
    esac
    # 无论什么原因失败，都把 APK 留给用户
    mkdir -p "$FALLBACK_DIR" 2>/dev/null
    cp -f "$APK" "$FALLBACK_DIR/ChargeControl.apk" 2>/dev/null \
      && ui_print "      已放到: $FALLBACK_DIR/ChargeControl.apk（可手动点击安装）"
  fi

  rm -f "$TMP_APK" 2>/dev/null
fi

ui_print "- 安装完成"
ui_print "  新版本已生效，无需重启手机"
if [ -f "$PERSIST_APK" ]; then
  ui_print "  App 已就位，后续可用 App 内「模块更新」升级"
fi
