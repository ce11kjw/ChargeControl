/* ChargeControl - 基于原版逻辑的优化版本 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MICROVOLTS_THRESHOLD 100000L
#define MICROAMPS_THRESHOLD  100000L

/* ── sysfs 路径表 ────────────────────────────────────────── */
static const char *PATHS_CAPACITY[] = {
    "/sys/class/power_supply/battery/capacity",
    "/sys/class/power_supply/BAT0/capacity",
    "/sys/class/power_supply/bms/capacity",
    NULL
};

static const char *PATHS_STATUS[] = {
    "/sys/class/power_supply/battery/status",
    "/sys/class/power_supply/BAT0/status",
    "/sys/class/power_supply/bms/status",
    NULL
};

static const char *PATHS_TEMP[] = {
    "/sys/class/power_supply/battery/temp",
    "/sys/class/power_supply/BAT0/temp",
    "/sys/class/power_supply/bms/temp",
    NULL
};

static const char *PATHS_VOLTAGE[] = {
    "/sys/class/power_supply/battery/voltage_now",
    "/sys/class/power_supply/BAT0/voltage_now",
    "/sys/class/power_supply/bms/voltage_now",
    NULL
};

static const char *PATHS_CURRENT[] = {
    "/sys/class/power_supply/battery/current_now",
    "/sys/class/power_supply/BAT0/current_now",
    "/sys/class/power_supply/bms/current_now",
    NULL
};

static const char *PATHS_HEALTH[] = {
    "/sys/class/power_supply/battery/health",
    "/sys/class/power_supply/BAT0/health",
    "/sys/class/power_supply/bms/health",
    NULL
};

/* ── 读取函数 ───────────────────────────────────────────── */
static int read_sysfs(const char **paths, char *buf, size_t bufsz) {
    for (int i = 0; paths[i]; i++) {
        FILE *fp = fopen(paths[i], "r");
        if (!fp) continue;
        size_t n = fread(buf, 1, bufsz - 1, fp);
        fclose(fp);
        if (n == 0) continue;
        buf[n] = '\0';
        /* 去除末尾空白 */
        while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r' ||
                         buf[n-1] == ' '  || buf[n-1] == '\t'))
            buf[--n] = '\0';
        return 0;
    }
    return -1;
}

/* ── 获取电池状态（JSON格式）────────────────────────────── */
char* get_battery_json(void) {
    char buf[64];
    char json[1024];
    int offset = 0;
    
    offset += snprintf(json + offset, sizeof(json) - offset, "{");
    
    /* 容量 */
    if (read_sysfs(PATHS_CAPACITY, buf, sizeof(buf)) == 0) {
        offset += snprintf(json + offset, sizeof(json) - offset,
                          "\"capacity\":%s,", buf);
    }
    
    /* 状态 */
    if (read_sysfs(PATHS_STATUS, buf, sizeof(buf)) == 0) {
        offset += snprintf(json + offset, sizeof(json) - offset,
                          "\"status\":\"%s\",", buf);
    }
    
    /* 健康 */
    if (read_sysfs(PATHS_HEALTH, buf, sizeof(buf)) == 0) {
        offset += snprintf(json + offset, sizeof(json) - offset,
                          "\"health\":\"%s\",", buf);
    }
    
    /* 温度：Android是十分之一度 */
    if (read_sysfs(PATHS_TEMP, buf, sizeof(buf)) == 0) {
        long val = atol(buf);
        double temp = (labs(val) > 100) ? val / 10.0 : (double)val;
        offset += snprintf(json + offset, sizeof(json) - offset,
                          "\"temperature\":%.1f,", temp);
    }
    
    /* 电压：根据阈值判断单位 */
    if (read_sysfs(PATHS_VOLTAGE, buf, sizeof(buf)) == 0) {
        long uv = atol(buf);
        double mv = (labs(uv) > MICROVOLTS_THRESHOLD) ? uv / 1000.0 : (double)uv;
        offset += snprintf(json + offset, sizeof(json) - offset,
                          "\"voltage_mv\":%.1f,", mv);
    }
    
    /* 电流：根据阈值判断单位 */
    if (read_sysfs(PATHS_CURRENT, buf, sizeof(buf)) == 0) {
        long ua = atol(buf);
        double ma = (labs(ua) > MICROAMPS_THRESHOLD) ? ua / 1000.0 : (double)ua;
        offset += snprintf(json + offset, sizeof(json) - offset,
                          "\"current_ma\":%.1f,", ma);
    }
    
    /* 时间戳 */
    offset += snprintf(json + offset, sizeof(json) - offset,
                       "\"timestamp\":%ld}", time(NULL));
    
    return strdup(json);
}
