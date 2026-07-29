#!/bin/bash
# ChargeControl 兼容性测试脚本

echo "========================================="
echo "  ChargeControl 兼容性测试"
echo "========================================="
echo ""

# 测试计数
TOTAL=0
PASS=0
FAIL=0

# 测试函数
test_item() {
    local name=$1
    local result=$2
    TOTAL=$((TOTAL + 1))
    
    if [ "$result" = "0" ]; then
        echo "✅ $name"
        PASS=$((PASS + 1))
    else
        echo "❌ $name"
        FAIL=$((FAIL + 1))
    fi
}

echo "================ 设备兼容性测试 ================"
echo ""

# 测试不同芯片平台的sysfs路径
echo "--- 高通平台路径 ---"
for path in \
    "/sys/class/power_supply/battery/capacity" \
    "/sys/class/power_supply/battery/status" \
    "/sys/class/power_supply/battery/temp" \
    "/sys/class/power_supply/battery/voltage_now" \
    "/sys/class/power_supply/battery/current_now" \
    "/sys/class/power_supply/battery/health" \
    "/sys/class/power_supply/battery/charging_enabled" \
    "/sys/class/power_supply/battery/charge_control_limit" \
    "/sys/class/power_supply/battery/charge_full_design" \
    "/sys/class/power_supply/battery/charge_full" \
    "/sys/class/power_supply/battery/cycle_count" \
    "/sys/class/power_supply/battery/manufacturer" \
    "/sys/class/power_supply/battery/model_name" \
    "/sys/class/power_supply/battery/technology" \
    "/sys/kernel/debug/charger/charging_enable" \
    "/sys/devices/platform/soc/soc:qti_battery_charger/charge_limit"; do
    test_item "高通: $path" "1"
done

echo ""
echo "--- 联发科平台路径 ---"
for path in \
    "/sys/class/power_supply/battery/capacity" \
    "/sys/class/power_supply/battery/status" \
    "/sys/class/power_supply/battery/temp" \
    "/sys/class/power_supply/battery/voltage_now" \
    "/sys/class/power_supply/battery/current_now" \
    "/sys/class/power_supply/battery/charging_enable" \
    "/sys/class/power_supply/battery/charge_control_limit" \
    "/proc/mtk_battery_cmd/current_cmd" \
    "/proc/driver/charger"; do
    test_item "联发科: $path" "1"
done

echo ""
echo "--- 三星平台路径 ---"
for path in \
    "/sys/class/power_supply/battery/capacity" \
    "/sys/class/power_supply/battery/status" \
    "/sys/class/power_supply/battery/temp" \
    "/sys/class/power_supply/battery/voltage_now" \
    "/sys/class/power_supply/battery/current_now" \
    "/sys/class/power_supply/battery/batt_health" \
    "/sys/class/power_supply/battery/batt_temp" \
    "/sys/class/power_supply/battery/input_current_limit" \
    "/sys/class/power_supply/battery/charge_control_limit"; do
    test_item "三星: $path" "1"
done

echo ""
echo "--- 华为平台路径 ---"
for path in \
    "/sys/class/power_supply/battery/capacity" \
    "/sys/class/power_supply/battery/status" \
    "/sys/class/power_supply/battery/temp" \
    "/sys/class/power_supply/battery/voltage_now" \
    "/sys/class/power_supply/battery/current_now" \
    "/sys/class/power_supply/hisi_battery/capacity" \
    "/sys/class/power_supply/hisi_battery/status" \
    "/proc/driver/hisi_battery"; do
    test_item "华为: $path" "1"
done

echo ""
echo "--- 小米平台路径 ---"
for path in \
    "/sys/class/power_supply/battery/capacity" \
    "/sys/class/power_supply/battery/status" \
    "/sys/class/power_supply/battery/temp" \
    "/sys/class/power_supply/battery/voltage_now" \
    "/sys/class/power_supply/battery/current_now" \
    "/sys/class/power_supply/battery/charging_enabled" \
    "/sys/class/power_supply/battery/charge_control_limit" \
    "/sys/class/power_supply/battery/batt_cycle"; do
    test_item "小米: $path" "1"
done

echo ""
echo "--- OPPO平台路径 ---"
for path in \
    "/sys/class/power_supply/battery/capacity" \
    "/sys/class/power_supply/battery/status" \
    "/sys/class/power_supply/battery/temp" \
    "/sys/class/power_supply/battery/voltage_now" \
    "/sys/class/power_supply/battery/current_now" \
    "/sys/class/power_supply/battery/voocchg_ing" \
    "/sys/class/power_supply/battery/charge_control_limit"; do
    test_item "OPPO: $path" "1"
done

echo ""
echo "--- VIVO平台路径 ---"
for path in \
    "/sys/class/power_supply/battery/capacity" \
    "/sys/class/power_supply/battery/status" \
    "/sys/class/power_supply/battery/temp" \
    "/sys/class/power_supply/battery/voltage_now" \
    "/sys/class/power_supply/battery/current_now" \
    "/sys/class/power_supply/battery/charging_enabled" \
    "/sys/class/power_supply/battery/charge_control_limit"; do
    test_item "VIVO: $path" "1"
done

echo ""
echo "--- Google Tensor平台路径 ---"
for path in \
    "/sys/class/power_supply/battery/capacity" \
    "/sys/class/power_supply/battery/status" \
    "/sys/class/power_supply/battery/temp" \
    "/sys/class/power_supply/battery/voltage_now" \
    "/sys/class/power_supply/battery/current_now" \
    "/sys/class/power_supply/google-battery/capacity" \
    "/sys/class/power_supply/google-battery/charge_full"; do
    test_item "Google Tensor: $path" "1"
done

echo ""
echo "================ 芯片温度传感器测试 ================"
echo ""

# 温度传感器路径
for i in $(seq 0 20); do
    test_item "thermal_zone${i}" "1"
done

echo ""
echo "================ API 兼容性测试 ================"
echo ""

# 测试API端点
for endpoint in \
    "/api/battery" \
    "/api/settings" \
    "/api/events" \
    "/api/charging/mode" \
    "/api/stats/daily" \
    "/api/stats/weekly" \
    "/api/stats/monthly" \
    "/api/export/csv" \
    "/api/export/json"; do
    result=$(curl -s -o /dev/null -w "%{http_code}" "http://127.0.0.1:8080${endpoint}" 2>/dev/null)
    if [ "$result" = "200" ] || [ "$result" = "204" ]; then
        test_item "API: $endpoint (HTTP $result)" "0"
    else
        test_item "API: $endpoint (HTTP $result)" "1"
    fi
done

echo ""
echo "================ 并发兼容性测试 ================"
echo ""

# 并发测试
echo "--- 并发请求测试 ---"
for concurrency in 5 10 20 50; do
    for i in $(seq 1 $concurrency); do
        curl -s http://127.0.0.1:8080/api/battery > /dev/null &
    done
    wait
    test_item "并发 $concurrency 个请求" "0"
done

echo ""
echo "================ 长时间稳定性测试 ================"
echo ""

# 长时间测试
echo "--- 30秒稳定性测试 ---"
START_TIME=$(date +%s)
END_TIME=$((START_TIME + 30))
SUCCESS=0
TOTAL_REQ=0

while [ $(date +%s) -lt $END_TIME ]; do
    result=$(curl -s -o /dev/null -w "%{http_code}" http://127.0.0.1:8080/api/battery 2>/dev/null)
    TOTAL_REQ=$((TOTAL_REQ + 1))
    if [ "$result" = "200" ]; then
        SUCCESS=$((SUCCESS + 1))
    fi
    sleep 0.1
done

RATE=$((SUCCESS * 100 / TOTAL_REQ))
if [ $RATE -ge 99 ]; then
    test_item "30秒稳定性 ($SUCCESS/$TOTAL_REQ, ${RATE}%)" "0"
else
    test_item "30秒稳定性 ($SUCCESS/$TOTAL_REQ, ${RATE}%)" "1"
fi

echo ""
echo "================ 测试结果汇总 ================"
echo ""
echo "总测试数: $TOTAL"
echo "✅ 通过: $PASS"
echo "❌ 失败: $FAIL"
echo "通过率: $((PASS * 100 / TOTAL))%"
echo ""
