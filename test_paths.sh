#!/bin/bash
# 电池路径和CPU路径测试脚本

echo "========================================="
echo "  电池路径 & CPU路径 测试脚本"
echo "========================================="
echo ""

# 统计变量
TOTAL=0
SUCCESS=0
FAIL=0

# 测试函数
test_path() {
    local path=$1
    local desc=$2
    TOTAL=$((TOTAL + 1))
    
    if [ -f "$path" ]; then
        value=$(cat "$path" 2>/dev/null | tr -d '\n')
        if [ -n "$value" ]; then
            echo "✅ $desc"
            echo "   路径: $path"
            echo "   值: $value"
            SUCCESS=$((SUCCESS + 1))
            return 0
        fi
    fi
    echo "❌ $desc"
    echo "   路径: $path (不存在或为空)"
    FAIL=$((FAIL + 1))
    return 1
}

echo "================ 电池路径测试 ================"
echo ""

# 电池容量
echo "--- 容量 (capacity) ---"
test_path "/sys/class/power_supply/battery/capacity" "标准电池"
test_path "/sys/class/power_supply/BAT0/capacity" "BAT0"
test_path "/sys/class/power_supply/BAT1/capacity" "BAT1"
test_path "/sys/class/power_supply/bms/capacity" "BMS"
test_path "/sys/class/power_supply/qcom-battery/capacity" "高通电池"

echo ""
echo "--- 状态 (status) ---"
test_path "/sys/class/power_supply/battery/status" "标准电池"
test_path "/sys/class/power_supply/BAT0/status" "BAT0"
test_path "/sys/class/power_supply/bms/status" "BMS"

echo ""
echo "--- 温度 (temp) ---"
test_path "/sys/class/power_supply/battery/temp" "标准温度"
test_path "/sys/class/power_supply/battery/batt_temp" "batt_temp"
test_path "/sys/class/power_supply/BAT0/temp" "BAT0温度"
test_path "/sys/class/power_supply/bms/temp" "BMS温度"

echo ""
echo "--- 电压 (voltage) ---"
test_path "/sys/class/power_supply/battery/voltage_now" "标准电压"
test_path "/sys/class/power_supply/battery/voltage_avg" "平均电压"
test_path "/sys/class/power_supply/BAT0/voltage_now" "BAT0电压"
test_path "/sys/class/power_supply/bms/voltage_now" "BMS电压"

echo ""
echo "--- 电流 (current) ---"
test_path "/sys/class/power_supply/battery/current_now" "标准电流"
test_path "/sys/class/power_supply/battery/current_avg" "平均电流"
test_path "/sys/class/power_supply/BAT0/current_now" "BAT0电流"
test_path "/sys/class/power_supply/bms/current_now" "BMS电流"

echo ""
echo "--- 健康 (health) ---"
test_path "/sys/class/power_supply/battery/health" "标准健康"
test_path "/sys/class/power_supply/BAT0/health" "BAT0健康"
test_path "/sys/class/power_supply/bms/health" "BMS健康"

echo ""
echo "--- 充电控制 ---"
test_path "/sys/class/power_supply/battery/charging_enabled" "充电开关"
test_path "/sys/class/power_supply/battery/charge_control_limit" "充电限制"
test_path "/sys/kernel/debug/charger/charging_enable" "高通充电开关"

echo ""
echo "--- 电池信息 ---"
test_path "/sys/class/power_supply/battery/charge_full_design" "设计容量"
test_path "/sys/class/power_supply/battery/charge_full" "当前容量"
test_path "/sys/class/power_supply/battery/cycle_count" "循环次数"
test_path "/sys/class/power_supply/battery/manufacturer" "制造商"
test_path "/sys/class/power_supply/battery/model_name" "型号"
test_path "/sys/class/power_supply/battery/technology" "技术"

echo ""
echo "================ CPU/芯片路径测试 ================"
echo ""

# CPU温度
echo "--- 芯片温度 ---"
for i in $(seq 0 15); do
    test_path "/sys/class/thermal/thermal_zone${i}/temp" "thermal_zone${i}"
done

echo ""
echo "--- CPU信息 ---"
test_path "/proc/cpuinfo" "CPU信息"
test_path "/sys/devices/system/cpu/possible" "CPU核心数"
test_path "/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq" "CPU频率"
test_path "/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq" "最大频率"

echo ""
echo "--- SoC信息 ---"
test_path "/sys/devices/soc/soc_id" "SoC ID"
test_path "/sys/devices/soc/revision" "SoC版本"

echo ""
echo "================ 测试结果汇总 ================"
echo ""
echo "总测试数: $TOTAL"
echo "✅ 成功: $SUCCESS"
echo "❌ 失败: $FAIL"
echo "成功率: $((SUCCESS * 100 / TOTAL))%"
echo ""
