/* Android 兼容性测试程序 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MICROVOLTS_THRESHOLD 100000L
#define MICROAMPS_THRESHOLD  100000L
#define BASE_PATH "/tmp/test_sysfs"

/* sysfs 路径表 */
static const char *PATHS_CAPACITY[] = {
    BASE_PATH "/class/power_supply/battery/capacity",
    BASE_PATH "/class/power_supply/bms/capacity",
    NULL
};

static const char *PATHS_STATUS[] = {
    BASE_PATH "/class/power_supply/battery/status",
    BASE_PATH "/class/power_supply/bms/status",
    NULL
};

static const char *PATHS_TEMP[] = {
    BASE_PATH "/class/power_supply/battery/temp",
    BASE_PATH "/class/power_supply/bms/temp",
    NULL
};

static const char *PATHS_VOLTAGE[] = {
    BASE_PATH "/class/power_supply/battery/voltage_now",
    BASE_PATH "/class/power_supply/bms/voltage_now",
    NULL
};

static const char *PATHS_CURRENT[] = {
    BASE_PATH "/class/power_supply/battery/current_now",
    BASE_PATH "/class/power_supply/bms/current_now",
    NULL
};

static const char *PATHS_HEALTH[] = {
    BASE_PATH "/class/power_supply/battery/health",
    BASE_PATH "/class/power_supply/bms/health",
    NULL
};

static const char *PATHS_MANUFACTURER[] = {
    BASE_PATH "/class/power_supply/battery/manufacturer",
    NULL
};

static const char *PATHS_MODEL[] = {
    BASE_PATH "/class/power_supply/battery/model_name",
    NULL
};

static const char *PATHS_TECHNOLOGY[] = {
    BASE_PATH "/class/power_supply/battery/technology",
    NULL
};

static const char *PATHS_CHIP_TEMP[] = {
    BASE_PATH "/class/thermal/thermal_zone0/temp",
    BASE_PATH "/class/thermal/thermal_zone1/temp",
    BASE_PATH "/class/thermal/thermal_zone2/temp",
    BASE_PATH "/class/thermal/thermal_zone3/temp",
    NULL
};

/* 读取函数 */
static int read_sysfs(const char **paths, char *buf, size_t bufsz) {
    for (int i = 0; paths[i]; i++) {
        FILE *fp = fopen(paths[i], "r");
        if (!fp) continue;
        size_t n = fread(buf, 1, bufsz - 1, fp);
        fclose(fp);
        if (n == 0) continue;
        buf[n] = '\0';
        while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r' ||
                         buf[n-1] == ' '  || buf[n-1] == '\t'))
            buf[--n] = '\0';
        return 0;
    }
    return -1;
}

int main() {
    char buf[64];
    int pass = 0, fail = 0;
    
    printf("=========================================\n");
    printf("  Android 兼容性测试\n");
    printf("=========================================\n\n");
    
    /* 测试容量 */
    printf("--- 电池容量 ---\n");
    if (read_sysfs(PATHS_CAPACITY, buf, sizeof(buf)) == 0) {
        int capacity = atoi(buf);
        printf("✅ 容量: %d%%\n", capacity);
        if (capacity >= 0 && capacity <= 100) {
            printf("✅ 容量范围验证: 通过\n");
            pass++;
        } else {
            printf("❌ 容量范围验证: 失败\n");
            fail++;
        }
        pass++;
    } else {
        printf("❌ 读取容量失败\n");
        fail++;
    }
    
    printf("\n--- 电池状态 ---\n");
    if (read_sysfs(PATHS_STATUS, buf, sizeof(buf)) == 0) {
        printf("✅ 状态: %s\n", buf);
        pass++;
    } else {
        printf("❌ 读取状态失败\n");
        fail++;
    }
    
    printf("\n--- 电池温度 ---\n");
    if (read_sysfs(PATHS_TEMP, buf, sizeof(buf)) == 0) {
        long val = atol(buf);
        double temp = (labs(val) > 100) ? val / 10.0 : (double)val;
        printf("✅ 原始值: %s\n", buf);
        printf("✅ 转换后: %.1f°C\n", temp);
        if (temp >= -40 && temp <= 120) {
            printf("✅ 温度范围验证: 通过\n");
            pass++;
        } else {
            printf("❌ 温度范围验证: 失败\n");
            fail++;
        }
        pass++;
    } else {
        printf("❌ 读取温度失败\n");
        fail++;
    }
    
    printf("\n--- 电池电压 ---\n");
    if (read_sysfs(PATHS_VOLTAGE, buf, sizeof(buf)) == 0) {
        long uv = atol(buf);
        double mv = (labs(uv) > MICROVOLTS_THRESHOLD) ? uv / 1000.0 : (double)uv;
        printf("✅ 原始值: %s μV\n", buf);
        printf("✅ 转换后: %.1f mV\n", mv);
        if (mv >= 3000 && mv <= 5000) {
            printf("✅ 电压范围验证: 通过\n");
            pass++;
        } else {
            printf("❌ 电压范围验证: 失败\n");
            fail++;
        }
        pass++;
    } else {
        printf("❌ 读取电压失败\n");
        fail++;
    }
    
    printf("\n--- 电池电流 ---\n");
    if (read_sysfs(PATHS_CURRENT, buf, sizeof(buf)) == 0) {
        long ua = atol(buf);
        double ma = (labs(ua) > MICROAMPS_THRESHOLD) ? ua / 1000.0 : (double)ua;
        printf("✅ 原始值: %s μA\n", buf);
        printf("✅ 转换后: %.1f mA\n", ma);
        pass++;
    } else {
        printf("❌ 读取电流失败\n");
        fail++;
    }
    
    printf("\n--- 电池健康 ---\n");
    if (read_sysfs(PATHS_HEALTH, buf, sizeof(buf)) == 0) {
        printf("✅ 健康状态: %s\n", buf);
        pass++;
    } else {
        printf("❌ 读取健康状态失败\n");
        fail++;
    }
    
    printf("\n--- 制造商 ---\n");
    if (read_sysfs(PATHS_MANUFACTURER, buf, sizeof(buf)) == 0) {
        printf("✅ 制造商: %s\n", buf);
        pass++;
    } else {
        printf("❌ 读取制造商失败\n");
        fail++;
    }
    
    printf("\n--- 型号 ---\n");
    if (read_sysfs(PATHS_MODEL, buf, sizeof(buf)) == 0) {
        printf("✅ 型号: %s\n", buf);
        pass++;
    } else {
        printf("❌ 读取型号失败\n");
        fail++;
    }
    
    printf("\n--- 电池技术 ---\n");
    if (read_sysfs(PATHS_TECHNOLOGY, buf, sizeof(buf)) == 0) {
        printf("✅ 技术: %s\n", buf);
        pass++;
    } else {
        printf("❌ 读取技术失败\n");
        fail++;
    }
    
    printf("\n--- 芯片温度 ---\n");
    for (int i = 0; PATHS_CHIP_TEMP[i]; i++) {
        FILE *fp = fopen(PATHS_CHIP_TEMP[i], "r");
        if (fp) {
            long temp_raw;
            if (fscanf(fp, "%ld", &temp_raw) == 1) {
                double temp = (temp_raw > 1000) ? temp_raw / 1000.0 : (double)temp_raw;
                printf("✅ thermal_zone%d: %.1f°C\n", i, temp);
                pass++;
            }
            fclose(fp);
        }
    }
    
    printf("\n=========================================\n");
    printf("  测试结果\n");
    printf("=========================================\n");
    printf("总测试: %d\n", pass + fail);
    printf("✅ 通过: %d\n", pass);
    printf("❌ 失败: %d\n", fail);
    printf("通过率: %d%%\n", (pass * 100) / (pass + fail));
    
    return (fail > 0) ? 1 : 0;
}
