/* ============================================
   动态电池路径发现 - 支持所有设备
   ============================================ */

#ifndef BATTERY_DISCOVERY_H
#define BATTERY_DISCOVERY_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

// 最大电池数（双电芯支持）
#define MAX_BATTERIES 4
#define MAX_PATH_LEN 512
#define MAX_VALUE_LEN 256

// 读取状态
typedef enum {
    READ_OK,
    READ_ERROR_NOT_FOUND,
    READ_ERROR_PERMISSION,
    READ_ERROR_EMPTY,
    READ_ERROR_FORMAT,
    READ_ERROR_RANGE,
    READ_ERROR_UNKNOWN
} ReadStatus;

// 电池信息
typedef struct {
    char name[64];
    char base_path[MAX_PATH_LEN];
    int exists;
} BatteryDevice;

// 电池属性路径
typedef struct {
    // 核心数据
    char capacity[MAX_PATH_LEN];
    char status[MAX_PATH_LEN];
    char temp[MAX_PATH_LEN];
    char voltage[MAX_PATH_LEN];
    char current[MAX_PATH_LEN];
    char health[MAX_PATH_LEN];
    
    // 充电控制
    char charging_enabled[MAX_PATH_LEN];
    char charge_control_limit[MAX_PATH_LEN];
    
    // 电芯信息
    char charge_full[MAX_PATH_LEN];
    char charge_full_design[MAX_PATH_LEN];
    char cycle_count[MAX_PATH_LEN];
    char manufacturer[MAX_PATH_LEN];
    char model_name[MAX_PATH_LEN];
    char technology[MAX_PATH_LEN];
    
    // 扩展
    char type[MAX_PATH_LEN];
    char input_current_limit[MAX_PATH_LEN];
    char constant_charge_current[MAX_PATH_LEN];
} BatteryPaths;

// 读取结果
typedef struct {
    ReadStatus status;
    char value[MAX_VALUE_LEN];
    char error[256];
} ReadResult;

// 全局变量
extern BatteryDevice g_batteries[MAX_BATTERIES];
extern int g_battery_count;
extern BatteryPaths g_battery_paths[MAX_BATTERIES];

// 函数声明

/**
 * 扫描所有电池设备
 * @return 发现的电池数量
 */
int discover_all_batteries(void);

/**
 * 为指定电池构建属性路径
 * @param battery_index 电池索引
 */
void build_battery_paths(int battery_index);

/**
 * 安全读取sysfs文件
 * @param path 文件路径
 * @param result 读取结果
 */
void safe_read_file(const char *path, ReadResult *result);

/**
 * 验证电量值
 * @param value 原始值
 * @return ReadStatus
 */
ReadStatus validate_capacity(const char *value);

/**
 * 验证温度值
 * @param value 原始值
 * @return ReadStatus
 */
ReadStatus validate_temperature(const char *value);

/**
 * 验证电压值
 * @param value 原始值
 * @return ReadStatus
 */
ReadStatus validate_voltage(const char *value);

/**
 * 验证电流值
 * @param value 原始值
 * @return ReadStatus
 */
ReadStatus validate_current(const char *value);

/**
 * 验证整数值
 * @param value 原始值
 * @param min 最小值
 * @param max 最大值
 * @return ReadStatus
 */
ReadStatus validate_integer(const char *value, int min, int max);

/**
 * 检测芯片温度
 * @param zone 温度区域索引
 * @param result 读取结果
 */
void read_chip_temperature(int zone, ReadResult *result);

/**
 * 读取CPU信息
 * @param result CPU型号结果
 */
void read_cpu_info(ReadResult *result);

/**
 * 读取CPU核心数
 * @param result 核心数结果
 */
void read_cpu_cores(ReadResult *result);

/**
 * 读取CPU频率
 * @param result 频率结果
 */
void read_cpu_freq(ReadResult *result);

/**
 * 打印发现报告
 */
void print_discovery_report(void);

#endif // BATTERY_DISCOVERY_H
