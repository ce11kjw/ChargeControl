#!/system/bin/sh
# ChargeControl post-fs-data —— 尽早建立内部目录并留痕（APatch/KSU/Magisk 通用）
MODDIR=${0%/*}
DIR=/data/adb/battery-manager
umask 077

mkdir -p "$DIR" 2>/dev/null
mkdir -p "$DIR/webroot" 2>/dev/null
{
  printf '%s post-fs-data: dir ready framework=%s\n' \
    "$(date -u '+%Y-%m-%dT%H:%M:%SZ' 2>/dev/null || echo unknown)" \
    "${KSU:+KSU}${APATCH:+APATCH}${MAGISK_VER:+Magisk}"
} >> /data/local/tmp/chargecontrol-boot.log 2>/dev/null
exit 0
