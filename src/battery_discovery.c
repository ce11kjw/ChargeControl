/* ============================================
   动态电池路径发现 - 支持所有设备
   ============================================ */

#include "battery_discovery.h"

// 全局变量
BatteryDevice g_batteries[MAX_BATTERIES];
int g_battery_count = 0;
BatteryPaths g_battery_paths[MAX_BATTERIES];

// 检查文件是否存在
#include <ctype.h>
static int file_exists(const char *path) {
    struct stat st;
    return (stat(path, &st) == 0 && S_ISREG(st.st_mode));
}

// 检查目录是否存在
static int dir_exists(const char *path) {
    struct stat st;
    return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
}

// 去除首尾空白
static void trim_whitespace(char *str) {
    char *end;
    while (isspace((unsigned char)*str)) str++;
    if (*str == 0) return;
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    *(end + 1) = 0;
}

// 检查是否是数字
static int is_numeric(const char *str) {
    if (!str || *str == 0) return 0;
    char *endptr;
    strtol(str, &endptr, 10);
    return (*endptr == 0);
}

// ============================================
// 发现所有电池设备
// ============================================
int discover_all_batteries(void) {
    DIR *dir = opendir("/sys/class/power_supply");
    if (!dir) {
        fprintf(stderr, "[电池发现] 无法打开 /sys/class/power_supply\n");
        return 0;
    }
    
    struct dirent *entry;
    g_battery_count = 0;
    
    while ((entry = readdir(dir)) != NULL && g_battery_count < MAX_BATTERIES) {
        if (entry->d_name[0] == '.') continue;
        
        char type_path[MAX_PATH_LEN];
        snprintf(type_path, sizeof(type_path), 
                 "/sys/class/power_supply/%s/type", entry->d_name);
        
        // 尝试读取type文件
        FILE *fp = fopen(type_path, "r");
        if (!fp) continue;
        
        char type_value[64] = {0};
        size_t n = fread(type_value, 1, sizeof(type_value) - 1, fp);
        fclose(fp);
        
        if (n > 0) {
            type_value[n] = '\0';
            trim_whitespace(type_value);
            
            // 检查是否是电池
            if (strcasecmp(type_value, "Battery") == 0 || 
                strcasecmp(type_value, "battery") == 0) {
                
                strncpy(g_batteries[g_battery_count].name, entry->d_name, 63);
                snprintf(g_batteries[g_battery_count].base_path, MAX_PATH_LEN,
                         "/sys/class/power_supply/%s", entry->d_name);
                g_batteries[g_battery_count].exists = 1;
                
                // 构建路径
                build_battery_paths(g_battery_count);
                
                g_battery_count++;
                printf("[电池发现] 找到电池设备: %s\n", entry->d_name);
            }
        }
    }
    closedir(dir);
    
    printf("[电池发现] 共发现 %d 个电池设备\n", g_battery_count);
    return g_battery_count;
}

// ============================================
// 构建电池属性路径
// ============================================
void build_battery_paths(int battery_index) {
    if (battery_index < 0 || battery_index >= g_battery_count) return;
    
    BatteryPaths *paths = &g_battery_paths[battery_index];
    const char *base = g_batteries[battery_index].base_path;
    char temp_path[MAX_PATH_LEN];
    
    // 清空所有路径
    memset(paths, 0, sizeof(BatteryPaths));
    
    // ========== 核心数据 ==========
    
    // 容量
    snprintf(temp_path, sizeof(temp_path), "%s/capacity", base);
    if (file_exists(temp_path)) strcpy(paths->capacity, temp_path);
    
    // 状态
    snprintf(temp_path, sizeof(temp_path), "%s/status", base);
    if (file_exists(temp_path)) strcpy(paths->status, temp_path);
    
    // 温度（多种文件名）
    const char *temp_files[] = {"temp", "batt_temp", "temp_battery", NULL};
    for (int i = 0; temp_files[i]; i++) {
        snprintf(temp_path, sizeof(temp_path), "%s/%s", base, temp_files[i]);
        if (file_exists(temp_path)) {
            strcpy(paths->temp, temp_path);
            break;
        }
    }
    
    // 电压（多种文件名）
    const char *voltage_files[] = {"voltage_now", "voltage_avg", "batt_voltage", "voltage_battery", NULL};
    for (int i = 0; voltage_files[i]; i++) {
        snprintf(temp_path, sizeof(temp_path), "%s/%s", base, voltage_files[i]);
        if (file_exists(temp_path)) {
            strcpy(paths->voltage, temp_path);
            break;
        }
    }
    
    // 电流（多种文件名）
    const char *current_files[] = {"current_now", "current_avg", "batt_current", "current_battery", NULL};
    for (int i = 0; current_files[i]; i++) {
        snprintf(temp_path, sizeof(temp_path), "%s/%s", base, current_files[i]);
        if (file_exists(temp_path)) {
            strcpy(paths->current, temp_path);
            break;
        }
    }
    
    // 健康
    snprintf(temp_path, sizeof(temp_path), "%s/health", base);
    if (file_exists(temp_path)) strcpy(paths->health, temp_path);
    
    // ========== 充电控制 ==========
    
    // 充电开关
    const char *charging_files[] = {"charging_enabled", "input_suspend", "charger_enabled", NULL};
    for (int i = 0; charging_files[i]; i++) {
        snprintf(temp_path, sizeof(temp_path), "%s/%s", base, charging_files[i]);
        if (file_exists(temp_path)) {
            strcpy(paths->charging_enabled, temp_path);
            break;
        }
    }
    
    // 备用路径
    if (strlen(paths->charging_enabled) == 0) {
        if (file_exists("/sys/kernel/debug/charger/charging_enable")) {
            strcpy(paths->charging_enabled, "/sys/kernel/debug/charger/charging_enable");
        } else if (file_exists("/proc/driver/mmi_battery/charging")) {
            strcpy(paths->charging_enabled, "/proc/driver/mmi_battery/charging");
        }
    }
    
    // 充电限制
    snprintf(temp_path, sizeof(temp_path), "%s/charge_control_limit", base);
    if (file_exists(temp_path)) {
        strcpy(paths->charge_control_limit, temp_path);
    } else if (file_exists("/sys/devices/platform/soc/soc:qti_battery_charger/charge_limit")) {
        strcpy(paths->charge_control_limit, "/sys/devices/platform/soc/soc:qti_battery_charger/charge_limit");
    }
    
    // ========== 电芯信息 ==========
    
    // 当前容量
    snprintf(temp_path, sizeof(temp_path), "%s/charge_full", base);
    if (file_exists(temp_path)) strcpy(paths->charge_full, temp_path);
    
    // 设计容量
    snprintf(temp_path, sizeof(temp_path), "%s/charge_full_design", base);
    if (file_exists(temp_path)) strcpy(paths->charge_full_design, temp_path);
    
    // 循环次数
    snprintf(temp_path, sizeof(temp_path), "%s/cycle_count", base);
    if (file_exists(temp_path)) strcpy(paths->cycle_count, temp_path);
    
    // 制造商
    snprintf(temp_path, sizeof(temp_path), "%s/manufacturer", base);
    if (file_exists(temp_path)) strcpy(paths->manufacturer, temp_path);
    
    // 型号
    snprintf(temp_path, sizeof(temp_path), "%s/model_name", base);
    if (file_exists(temp_path)) strcpy(paths->model_name, temp_path);
    
    // 电池技术
    snprintf(temp_path, sizeof(temp_path), "%s/technology", base);
    if (file_exists(temp_path)) strcpy(paths->technology, temp_path);
    
    // ========== 扩展 ==========
    
    // 充电器类型
    snprintf(temp_path, sizeof(temp_path), "%s/type", base);
    if (file_exists(temp_path)) strcpy(paths->type, temp_path);
    
    // 输入电流限制
    const char *input_files[] = {"input_current_limit", "input_current", NULL};
    for (int i = 0; input_files[i]; i++) {
        snprintf(temp_path, sizeof(temp_path), "%s/%s", base, input_files[i]);
        if (file_exists(temp_path)) {
            strcpy(paths->input_current_limit, temp_path);
            break;
        }
    }
    
    // 恒定充电电流
    const char *cc_files[] = {"constant_charge_current", "constant_charge_current_max", NULL};
    for (int i = 0; cc_files[i]; i++) {
        snprintf(temp_path, sizeof(temp_path), "%s/%s", base, cc_files[i]);
        if (file_exists(temp_path)) {
            strcpy(paths->constant_charge_current, temp_path);
            break;
        }
    }
}

// ============================================
// 安全读取文件
// ============================================
void safe_read_file(const char *path, ReadResult *result) {
    memset(result, 0, sizeof(ReadResult));
    
    if (!path || strlen(path) == 0) {
        result->status = READ_ERROR_NOT_FOUND;
        strcpy(result->error, "路径为空");
        return;
    }
    
    // 检查文件是否存在
    if (!file_exists(path)) {
        result->status = READ_ERROR_NOT_FOUND;
        snprintf(result->error, sizeof(result->error), "文件不存在: %s", path);
        return;
    }
    
    // 检查是否可读
    if (access(path, R_OK) != 0) {
        result->status = READ_ERROR_PERMISSION;
        snprintf(result->error, sizeof(result->error), "权限不足: %s", path);
        return;
    }
    
    // 读取文件
    FILE *fp = fopen(path, "r");
    if (!fp) {
        result->status = READ_ERROR_UNKNOWN;
        snprintf(result->error, sizeof(result->error), "打开失败: %s (%s)", path, strerror(errno));
        return;
    }
    
    size_t n = fread(result->value, 1, MAX_VALUE_LEN - 1, fp);
    fclose(fp);
    
    if (n == 0) {
        result->status = READ_ERROR_EMPTY;
        snprintf(result->error, sizeof(result->error), "内容为空: %s", path);
        return;
    }
    
    result->value[n] = '\0';
    trim_whitespace(result->value);
    
    if (strlen(result->value) == 0) {
        result->status = READ_ERROR_EMPTY;
        snprintf(result->error, sizeof(result->error), "内容为空: %s", path);
        return;
    }
    
    result->status = READ_OK;
    result->error[0] = '\0';
}

// ============================================
// 验证函数
// ============================================

ReadStatus validate_capacity(const char *value) {
    if (!value || strlen(value) == 0) return READ_ERROR_EMPTY;
    
    for (int i = 0; value[i]; i++) {
        if (!isdigit(value[i])) return READ_ERROR_FORMAT;
    }
    
    int val = atoi(value);
    if (val < 0 || val > 100) return READ_ERROR_RANGE;
    
    return READ_OK;
}

ReadStatus validate_temperature(const char *value) {
    if (!value || strlen(value) == 0) return READ_ERROR_EMPTY;
    
    char *endptr;
    double val = strtod(value, &endptr);
    
    if (endptr == value) return READ_ERROR_FORMAT;
    if (val < -40.0 || val > 120.0) return READ_ERROR_RANGE;
    
    return READ_OK;
}

ReadStatus validate_voltage(const char *value) {
    if (!value || strlen(value) == 0) return READ_ERROR_EMPTY;
    
    // 允许负数（放电）
    char *endptr;
    long val = strtol(value, &endptr, 10);
    
    if (endptr == value) return READ_ERROR_FORMAT;
    
    // 电压范围检查（μV或mV）
    if (labs(val) > 100000000) return READ_ERROR_RANGE; // 明显异常
    
    return READ_OK;
}

ReadStatus validate_current(const char *value) {
    if (!value || strlen(value) == 0) return READ_ERROR_EMPTY;
    
    char *endptr;
    long val = strtol(value, &endptr, 10);
    
    if (endptr == value) return READ_ERROR_FORMAT;
    
    // 电流范围检查（μA或mA）
    if (labs(val) > 100000000) return READ_ERROR_RANGE; // 明显异常
    
    return READ_OK;
}

ReadStatus validate_integer(const char *value, int min, int max) {
    if (!value || strlen(value) == 0) return READ_ERROR_EMPTY;
    
    for (int i = 0; value[i]; i++) {
        if (!isdigit(value[i])) return READ_ERROR_FORMAT;
    }
    
    int val = atoi(value);
    if (val < min || val > max) return READ_ERROR_RANGE;
    
    return READ_OK;
}

// ============================================
// 芯片信息读取
// ============================================

void read_chip_temperature(int zone, ReadResult *result) {
    char path[MAX_PATH_LEN];
    snprintf(path, sizeof(path), "/sys/class/thermal/thermal_zone%d/temp", zone);
    safe_read_file(path, result);
}

void read_cpu_info(ReadResult *result) {
    // 尝试多种路径
    const char *paths[] = {
        "/sys/devices/soc/soc_id",
        "/proc/device-tree/compatible",
        NULL
    };
    
    for (int i = 0; paths[i]; i++) {
        safe_read_file(paths[i], result);
        if (result->status == READ_OK) return;
    }
    
    result->status = READ_ERROR_NOT_FOUND;
    strcpy(result->error, "未找到CPU信息");
}

void read_cpu_cores(ReadResult *result) {
    safe_read_file("/sys/devices/system/cpu/possible", result);
    if (result->status == READ_OK) {
        // 解析 "0-7" 格式
        int max_core = 0;
        if (sscanf(result->value, "%d", &max_core) == 1) {
            snprintf(result->value, sizeof(result->value), "%d", max_core + 1);
        }
    }
}

void read_cpu_freq(ReadResult *result) {
    safe_read_file("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq", result);
    if (result->status == READ_OK) {
        // 转换为MHz
        long freq_khz = strtol(result->value, NULL, 10);
        snprintf(result->value, sizeof(result->value), "%ld", freq_khz / 1000);
    }
}

// ============================================
// 打印发现报告
// ============================================
void print_discovery_report(void) {
    printf("\n");
    printf("═══════════════════════════════════════════════════\n");
    printf("  电池发现报告\n");
    printf("═══════════════════════════════════════════════════\n\n");
    
    for (int i = 0; i < g_battery_count; i++) {
        BatteryPaths *paths = &g_battery_paths[i];
        
        printf("电池 %d: %s\n", i + 1, g_batteries[i].name);
        printf("───────────────────────────────────────────────────\n");
        
        // 核心数据
        printf("  核心数据:\n");
        printf("    容量: %s\n", strlen(paths->capacity) ? "✓ 有" : "✗ 无");
        printf("    状态: %s\n", strlen(paths->status) ? "✓ 有" : "✗ 无");
        printf("    温度: %s\n", strlen(paths->temp) ? "✓ 有" : "✗ 无");
        printf("    电压: %s\n", strlen(paths->voltage) ? "✓ 有" : "✗ 无");
        printf("    电流: %s\n", strlen(paths->current) ? "✓ 有" : "✗ 无");
        printf("    健康: %s\n", strlen(paths->health) ? "✓ 有" : "✗ 无");
        
        // 充电控制
        printf("  充电控制:\n");
        printf("    充电开关: %s\n", strlen(paths->charging_enabled) ? "✓ 有" : "✗ 无");
        printf("    充电限制: %s\n", strlen(paths->charge_control_limit) ? "✓ 有" : "✗ 无");
        
        // 电芯信息
        printf("  电芯信息:\n");
        printf("    设计容量: %s\n", strlen(paths->charge_full_design) ? "✓ 有" : "✗ 无");
        printf("    当前容量: %s\n", strlen(paths->charge_full) ? "✓ 有" : "✗ 无");
        printf("    循环次数: %s\n", strlen(paths->cycle_count) ? "✓ 有" : "✗ 无");
        printf("    制造商: %s\n", strlen(paths->manufacturer) ? "✓ 有" : "✗ 无");
        printf("    型号: %s\n", strlen(paths->model_name) ? "✓ 有" : "✗ 无");
        printf("    技术: %s\n", strlen(paths->technology) ? "✓ 有" : "✗ 无");
        
        printf("\n");
    }
    
    // 芯片温度
    printf("芯片温度传感器:\n");
    printf("───────────────────────────────────────────────────\n");
    for (int i = 0; i < 10; i++) {
        ReadResult result;
        read_chip_temperature(i, &result);
        if (result.status == READ_OK) {
            printf("  thermal_zone%d: %s°C\n", i, result.value);
        }
    }
    
    printf("\n═══════════════════════════════════════════════════\n\n");
}
