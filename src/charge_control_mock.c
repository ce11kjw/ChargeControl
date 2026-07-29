/* ChargeControl - 模拟数据版本（用于测试） */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MICROVOLTS_THRESHOLD 100000L
#define MICROAMPS_THRESHOLD  100000L

/* 模拟数据生成 */
static int mock_capacity(void) {
    return 40 + (int)(time(NULL) % 51);  /* 40-90% */
}

static double mock_temperature(void) {
    return 25.0 + (double)(time(NULL) % 16);  /* 25-40°C */
}

static const char* mock_status(void) {
    const char *statuses[] = {"Charging", "Discharging", "Full", "Not charging"};
    return statuses[time(NULL) % 4];
}

static const char* mock_health(void) {
    return "Good";
}

static double mock_voltage(void) {
    return 3700.0 + (time(NULL) % 500);  /* 3700-4200 mV */
}

static double mock_current(void) {
    return 500.0 + (time(NULL) % 2000);  /* 500-2500 mA */
}

/* ── 获取电池状态（JSON格式）────────────────────────────── */
char* get_battery_json(void) {
    char json[1024];
    
    int capacity = mock_capacity();
    double temp = mock_temperature();
    double voltage = mock_voltage();
    double current = mock_current();
    double power = (voltage * current) / 1000000.0;
    
    snprintf(json, sizeof(json),
        "{"
        "\"capacity\":%d,"
        "\"status\":\"%s\","
        "\"health\":\"%s\","
        "\"temperature\":%.1f,"
        "\"voltage_mv\":%.1f,"
        "\"current_ma\":%.1f,"
        "%.2f\"power\":%.2f,"
        "\"charging_enabled\":true,"
        "\"timestamp\":%ld"
        "}",
        capacity,
        mock_status(),
        mock_health(),
        temp,
        voltage,
        current,
        power,
        time(NULL));
    
    return strdup(json);
}

/* ── 充电模式设置 ─────────────────────────────────────── */
int set_charging_mode(const char *mode) {
    printf("设置充电模式: %s\n", mode);
    return 0;
}

/* ── 获取芯片温度 ─────────────────────────────────────── */
double get_chip_temperature(void) {
    return 35.0 + (time(NULL) % 10);  /* 35-45°C */
}

/* ── 获取完整设置 JSON ───────────────────────────────── */
char* get_settings_json(void) {
    char *battery = get_battery_json();
    double chip_temp = get_chip_temperature();
    
    char json[2048];
    snprintf(json, sizeof(json),
        "{\"battery\":%s,"
        "\"extended\":{\"chip_temp\":%.1f,"
        "\"cpu_cores\":8,"
        "\"cpu_freq_mhz\":2800},"
        "\"config\":{\"charging\":{\"mode\":\"standard\",\"max_limit\":80,"
        "\"modes\":{"
        "\"turbo\":{\"max_current_ma\":12000,\"max_voltage_mv\":20000,\"description\":\"240W涡轮加速\"},"
        "\"fast\":{\"max_current_ma\":6000,\"max_voltage_mv\":20000,\"description\":\"120W快速充电\"},"
        "\"standard\":{\"max_current_ma\":3000,\"max_voltage_mv\":20000,\"description\":\"60W标准充电\"},"
        "\"trickle\":{\"max_current_ma\":1000,\"max_voltage_mv\":20000,\"description\":\"20W涓流充电\"},"
        "\"protect\":{\"max_current_ma\":500,\"max_voltage_mv\":20000,\"description\":\"10W电池保护\"}"
        "}}}}",
        battery, chip_temp);
    
    free(battery);
    return strdup(json);
}
