#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <ctype.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <dirent.h>
#include <sys/statfs.h>
#include <sys/select.h>

#define PORT        8800
#define SYSFS       "/sys/class/power_supply/battery"
#define SYSFS_USB   "/sys/class/power_supply/usb"
#define THERMAL     "/sys/class/thermal"
#define CONFIG      "/data/adb/battery-manager/batt.conf"
#define HISTORY     "/data/adb/battery-manager/history.json"
#define LOGFILE     "/data/adb/battery-manager/battd.log"
#define WEBROOT     "/data/adb/battery-manager/webroot"
#define MAX_LINE    4096
#define HIST_MAX    500
#define FULL_TIMEOUT 1800
#define VERSION "v1.3.1"

static volatile int running = 1;
static int charge_limit = 80;
static int temp_limit = 45;
static int chg_temp_limit = 60;     /* 充电IC 温度上限 */
/* ═══ v1.3.1 P2 新增能力（全部默认关闭，opt-in）═══ */
static int x_en_power_path = 0;     /* 真旁路：关电源路径（0=关功能,1=开启旁路） */
static int x_set_cv_en = 0;         /* 恒压点调整开关 */
static int x_set_cv_mv = 0;         /* 目标恒压点 mV (0=不改) */
static int x_limit_current_en = 0;  /* 充电电流限流开关 */
static int x_limit_current_ma = 0;  /* 目标电流 mA (0=不改) */
static int x_use_native_night = 0;  /* 用系统原生夜间充电 */
static int x_bypass_min_soc = 50;   /* 旁路最低电量护栏（低于此不允许旁路）*/
static int x_orig_cv = -1;          /* 原始恒压点备份（用于还原）*/
static int chg_temp_suspended = 0;  /* 新增：充电IC 过温标志 */
static int resume_delta = 5;
static int interval = 5;
static int limit_enabled = 1;
static int temp_suspended = 0;
static time_t last_save = 0;
static int full_once = 0;
static time_t full_until = 0;
static int history_enabled = 1;
static int paused = 0;
static int last_suspend_val = -1;
static long prev_cpu_ticks = 0;
static long prev_proc_ticks = 0;
static char proc_name_buf[32] = "";
static int proc_pid_val = 0;
static int cpu_pct = 0;
static int bypass_supported = 0;
static char bypass_node[64] = "";
static int bypass_soft = 0;
#define BYPASS_LO 55
#define BYPASS_HI 60
/* 充电统计 */
static time_t sess_start = 0;
static int sess_start_cap = -1;
static int sess_active = 0;
static int sess_min = 0, sess_mah = 0;
static long sess_start_counter = -1;
static int stats_count = 0;
static int night_enabled = 0;
static int night_start_h = 23, night_end_h = 7;
static int scene = 0;
static char webhook_url[256] = "";
static int lang = 0;
static int theme = 0;
static time_t session_30s = 0;
static int alerted_high = 0;
static long stats_min = 0, stats_mah = 0;

static char bat_technology[16] = "", bat_capacity_level[16] = "", bat_health[16] = "";
static int bat_charge_type = -1, bat_time_to_full = -1, bat_charge_counter = -1, bat_input_current_limit = -1;
static char bat_manufacturer[64] = "", bat_model_name[64] = "";
static int voltage_ocv = -1, current_avg = -1, temp_ambient = -1;
static int charge_now = -1, charge_term_current = -1, constant_charge_current_max = -1;
static int constant_charge_voltage_max = -1, temp_max = -1, temp_min = -1;
static int charger_temp = -1, charge_done = -1, input_voltage_settled = -1;
static int safety_timer_expired = -1, calibrate = -1;
static int constant_charge_current = -1, constant_charge_voltage = -1;
static int capacity_error_margin = -1, time_to_empty = -1, bat_present = -1, internal_resistance = -1;
static int mem_total = -1, mem_avail = -1, mem_free = -1;
/* ═══ v1.6.0 为 App 提供完整系统信息（App 改为纯模块取数，不再自读 sysfs）═══ */
#define MAX_CORES 16
static int  cpu_freq_khz[MAX_CORES] = {0};
static int  cpu_freq_max_khz[MAX_CORES] = {0};
static int  cpu_freq_min_khz[MAX_CORES] = {0};
static char cpu_arch[64] = "";
static char cpu_abi[128] = "";
static char cpu_cache_l1[32] = "", cpu_cache_l2[32] = "", cpu_cache_l3[32] = "";
static int  cpu_usage_total = -1;
static long cpu_ticks_prev_total = 0, cpu_ticks_prev_idle = 0;
static int  cpu_deep_sleep_pct = -1;
static int  cpu_idle_pct = -1;

static int mem_buffers = -1, mem_cached = -1, mem_sreclaim = -1, mem_sunreclaim = -1;
static int mem_shmem = -1, mem_pagetables = -1, mem_kernelstack = -1, mem_vmallocused = -1;
static int mem_swap_total = -1, mem_swap_free = -1;
static int mem_zram_orig = -1, mem_zram_compr = -1, mem_zram_used = -1;
static int mem_slab = -1, mem_mapped = -1;

#define MAX_PARTS 8
static int  part_count = 0;
static char part_mount[MAX_PARTS][48];
static long part_total_mb[MAX_PARTS], part_free_mb[MAX_PARTS];

static int  sys_sdk_level = -1;
static char sys_android_ver[32] = "", sys_build_display[64] = "", sys_security_patch[32] = "";
static char sys_bootloader[64] = "";
static char sys_soc_vendor[64] = "", sys_soc_model[64] = "";
/* ═══ v1.6.0 GPU（供 App GPU 页走模块取数）═══ */
/* ═══ v1.7.0 OEM / 厂商信息（对齐 App 的 OemInfo）═══ */
/* ═══ v1.8.0 硬件 / 设备详情（对齐 App DeviceDetailInfo 中模块可取的部分）═══ */
static char dev_storage_type[64] = "";      /* UFS 4.0 / eMMC 5.1（含型号） */
static char dev_memory_type[32] = "";       /* LPDDR5X / LPDDR5 / LPDDR4X */
static int  dev_memory_speed_mhz = -1;
static char dev_soc_chipname[64] = "";      /* SM8650 / MT6989 */
static char dev_soc_process_node[16] = "";  /* 4nm / 3nm */
static char dev_usb_version[24] = "";
static int  dev_usb_typec = -1, dev_usb_host = -1;
static char dev_bluetooth_ver[16] = "";
static int  dev_wifi_6e = -1, dev_wifi_ax = -1, dev_wifi_be = -1;
static char dev_wifi_standard[24] = "";
static char dev_widevine_level[24] = "";
static char dev_selinux[16] = "";
static char dev_crypto_state[24] = "";      /* encrypted / unencrypted */
static char dev_crypto_type[32] = "";       /* file / file-based */
static int  dev_bootloader_locked = -1;
static int  dev_tee_supported = -1;
static char dev_tee_version[32] = "";
static char dev_openssl[40] = "";
static char dev_gles_version[16] = "";
static char dev_vulkan_level[16] = "";
static char dev_hardware_egl[64] = "";
static char dev_serial[48] = "";
static char dev_display_type[32] = "";
static char dev_display_bit[16] = "";
static char dev_display_gamut[32] = "";
static char dev_display_series[64] = "";
static int  dev_has_nfc = -1, dev_has_ir = -1, dev_has_uwb = -1, dev_has_wireless_chg = -1;
static int  dev_screen_w = -1, dev_screen_h = -1;
static int  dev_screen_density = -1;
static int  dev_thermal_zones = 0;
static char dev_thermal_types[512] = "";
static char dev_audio_hires[16] = "";
static int  dev_dual_sim = -1;
static char dev_sim_operator[48] = "";

static char oem_brand[64] = "";          /* Xiaomi / OPPO / vivo / Samsung / AOSP */
static char oem_os_name[64] = "";        /* HyperOS / ColorOS / OriginOS / One UI */
static char oem_os_version[64] = "";
static char oem_build_fingerprint[192] = "";
static char oem_charging_protocol[64] = "";
static char oem_charging_watt[32] = "";
static char oem_battery_capacity[32] = "";
static int  oem_dual_cell = 0;
static char oem_display_panel[64] = "";      /* AMOLED / LCD */
static char oem_display_vendor[64] = "";     /* Samsung / BOE / Visionox */
static char oem_display_max_hz[16] = "";
static char oem_display_peak_nits[16] = "";
static char oem_cpu_governor[64] = "";
static char oem_gpu_governor[64] = "";
static char oem_security_chip[64] = "";
static int  oem_secure_boot = 0;
static char oem_verified_boot[16] = "";
static char oem_game_mode[8] = "";
static char oem_perf_mode[8] = "";
static char oem_power_save[8] = "";
static char oem_ultra_power_save[8] = "";
static char oem_ai_engine[64] = "";
static char oem_memory_fusion[32] = "";
static char oem_thermal_solution[64] = "";
static char oem_storage_boost[32] = "";

static char gpu_model[128] = "";
static char gpu_vendor[64] = "";
static int  gpu_freq_khz = -1, gpu_freq_min_khz = -1, gpu_freq_max_khz = -1;
static char gpu_governor[64] = "", gpu_avail_gov[192] = "";
static int  gpu_load_pct = -1;          /* GPU 负载 % ×10 */
static char gpu_load_src[32] = "";
static int  gpu_temp_c = -1;            /* 复用 thermal 已采集的 gpu_temp */
static int  gpu_throttled = 0;


static int storage_total = -1, storage_free = -1;
static int uptime_secs = -1, cpu_cores = -1;
static char kernel_ver[128] = "", cpu_model[128] = "", device_model[128] = "";
static int wifi_rssi = -128, wifi_on = 0, mobile_on = 0;
static char ip_addr[64] = "";
static int charge_full_mah = 0, design_mah = 5380, capacity_disp = 0, power_mw = 0, est_full_min = 0, est_cycles_left = 0;
static int soh_native = -1;   /* bms/soh 原生健康度 */
static char est_full_txt[32] = "";
static float health_degrade = 0.0f;
static char health_rating[16] = "";
static int bat_capacity = -1, bat_temp = -1, bat_volt = -1, bat_curr = -1;

/* ═══════════════ v1.5.0 充电曲线 & 校准 ═══════════════ */
#define SESSIONS_IDX "/data/adb/battery-manager/sessions.json"
#define SESSION_FILE "/data/adb/battery-manager/session.jsonl"
#define CALIB_FILE   "/data/adb/battery-manager/calibrate.jsonl"

/* 充电阶段：0=未充 1=预充 2=恒流CC 3=恒压CV 4=充满 */
static int chg_stage = 0;
static int prev_curr = 0;           /* 上一次电流，用于判断 CC/CV 拐点 */
static int cc_peak = 0;             /* 本轮充电的电流峰值（CV 判据基准 + 迟滞） */
static int prev_volt = 0;

/* 校准状态机：0=idle 1=charging_full 2=resting 3=discharging 4=charging_up 5=done */
static int cal_state = 0;
static time_t cal_started = 0;
static int cal_max_cap = -1;        /* 校准期间记录的最高/最低电量 */
static int cal_min_cap = 101;
static int cal_fulfilled = 0;       /* 是否已达 100% */

static void save_cal_state(void);
static void load_cal_state(void);
static char bat_status[32] = "Unknown";
static int soc_temp = -1, gpu_temp = -1, chg_temp = -1, case_temp = -1;
/* ★ 新增：MTK/PD 扩展字段 */
static int pd_pdo_count = 0;           /* PD 能力档位数 */
static int pd_pdo_max_mv = 0;          /* PD 最高电压(mV) */
static int pd_pdo_max_ma = 0;          /* PD 最大电流(mA) */
static char pd_svid[16] = "";          /* 适配器厂商标识 */
static char pd_state[24] = "";         /* PD 状态机状态 */
static int cc_limit_supported = 0;     /* charge_control_limit 是否可用 */
static int cc_limit_val = -1;          /* MTK 原生电量上限原值 */
static int power_path_en = -1;         /* en_power_path 旁路电源路径 */
static int jeita_index = -1;           /* JEITA 温控档位 */
static int soh_delta_x10 = 0;          /* SOH 相对首次记录的变化(x10) */
static int soh_first = -1;             /* 首次记录的 SOH */
static int cycle_count = -1, health = -1;
static int usb_online = 0, usb_power_max = 0, pd_type = 0;
static int usb_curr_cache = 0, usb_volt_cache = 0;
static char proto_name[32] = "未知";
static char control_state[24] = "idle";


/* ★ v1.3.1: JSON 字符串转义（sysfs 值含引号/反斜杠会破坏 JSON） */
static void json_escape(const char *in, char *out, int outsz) {
    int j = 0;
    for (int i = 0; in[i] && j < outsz - 2; i++) {
        unsigned char c = (unsigned char)in[i];
        if (c == '"' || c == '\\') { out[j++] = '\\'; out[j++] = c; }
        else if (c == '\n') { out[j++] = '\\'; out[j++] = 'n'; }
        else if (c == '\r') { out[j++] = '\\'; out[j++] = 'r'; }
        else if (c == '\t') { out[j++] = '\\'; out[j++] = 't'; }
        else if (c < 0x20) { /* 丢弃控制字符 */ }
        else out[j++] = c;
    }
    out[j] = '\0';
}

static void url_decode(char *s) {
    char *w = s;
    while (*s) {
        if (*s == '%' && isxdigit((unsigned char)s[1]) && isxdigit((unsigned char)s[2])) {
            int hi = isdigit(s[1]) ? s[1]-'0' : (tolower(s[1])-'a'+10);
            int lo = isdigit(s[2]) ? s[2]-'0' : (tolower(s[2])-'a'+10);
            *w++ = (char)((hi<<4)|lo); s += 3;
        } else if (*s == '+') { *w++ = ' '; s++; }
        else *w++ = *s++;
    }
    *w = '\0';
}

static int read_int(const char *path) {
    char buf[64]; int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    int n = read(fd, buf, sizeof(buf)-1);
    close(fd);
    if (n <= 0) return -1;
    buf[n] = '\0';
    return atoi(buf);
}
static int read_str(const char *path, char *out, int size) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    int n = read(fd, out, size-1);
    close(fd);
    if (n <= 0) return -1;
    out[n] = '\0';
    while (n > 0 && (out[n-1]=='\n'||out[n-1]=='\r')) out[--n] = '\0';
    return n;
}
/* ★ v1.4.0 关键修复：返回值语义。
   原实现 return n（write 的字节数），但调用方混用两种判定：
     5 处  if (write_str(...) == 0)   判成功
    11 处  if (write_str(...) <  0)   判失败
   写入成功时 n=1..8，于是「== 0」永远为假 —— 所有写入都被当成失败：
     · apply_power_path 写完即自我放弃（x_en_power_path=0），真旁路从未生效
     · apply_night_mode / input_suspend / 温度保护 的失败日志全是误报
   现在统一为：成功返回 0，失败返回 -1（POSIX 惯例），两种调用点都正确。
   另补 O_TRUNC：procfs/sysfs 不要求，但普通文件下不截断会残留旧内容。 */
static int write_str(const char *path, const char *val) {
    int fd = open(path, O_WRONLY | O_TRUNC);
    if (fd < 0) return -1;
    ssize_t n = write(fd, val, strlen(val));
    int r = (n == (ssize_t)strlen(val)) ? 0 : -1;
    close(fd);
    return r;
}

static void log_event(const char *msg) {
    struct stat st;
    if (stat(LOGFILE, &st) == 0 && st.st_size > 256 * 1024)
        rename(LOGFILE, "/data/adb/battery-manager/battd.log.old");
    FILE *f = fopen(LOGFILE, "a");
    if (!f) return;
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    fprintf(f, "[%04d-%02d-%02d %02d:%02d:%02d] %s\n",
            tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday,
            tm->tm_hour, tm->tm_min, tm->tm_sec, msg);
    fclose(f);
}

static time_t temps_last = 0;
static time_t slow_bat_last = 0;
static void slow_update_battery(void) {
    time_t tn = time(NULL);
    if (tn - slow_bat_last < 30) return;
    slow_bat_last = tn;
    char path[MAX_LINE]; int v;
    snprintf(path, sizeof(path), "%s/capacity_level", SYSFS); read_str(path, bat_capacity_level, sizeof(bat_capacity_level));
    snprintf(path, sizeof(path), "%s/health", SYSFS); read_str(path, bat_health, sizeof(bat_health));
    snprintf(path, sizeof(path), "%s/technology", SYSFS); read_str(path, bat_technology, sizeof(bat_technology));
    snprintf(path, sizeof(path), "%s/charge_type", SYSFS); if ((v = read_int(path)) >= 0) bat_charge_type = v;
    snprintf(path, sizeof(path), "%s/time_to_full_now", SYSFS); if ((v = read_int(path)) >= 0) bat_time_to_full = v;
    snprintf(path, sizeof(path), "%s/charge_counter", SYSFS); if ((v = read_int(path)) >= 0) bat_charge_counter = v;
    snprintf(path, sizeof(path), "%s/manufacturer", SYSFS); read_str(path, bat_manufacturer, sizeof(bat_manufacturer));
    snprintf(path, sizeof(path), "%s/model_name", SYSFS); read_str(path, bat_model_name, sizeof(bat_model_name));
    snprintf(path, sizeof(path), "%s/voltage_ocv", SYSFS); if ((v = read_int(path)) >= 0) voltage_ocv = v;
    snprintf(path, sizeof(path), "%s/current_avg", SYSFS); if ((v = read_int(path)) >= 0) current_avg = v;
    snprintf(path, sizeof(path), "%s/constant_charge_current", SYSFS); if ((v = read_int(path)) >= 0) constant_charge_current = v;
    snprintf(path, sizeof(path), "%s/constant_charge_voltage", SYSFS); if ((v = read_int(path)) >= 0) constant_charge_voltage = v;
    snprintf(path, sizeof(path), "%s/capacity_error_margin", SYSFS); if ((v = read_int(path)) >= 0) capacity_error_margin = v;
    snprintf(path, sizeof(path), "%s/time_to_empty_now", SYSFS); if ((v = read_int(path)) >= 0) time_to_empty = v;
    snprintf(path, sizeof(path), "%s/present", SYSFS); if ((v = read_int(path)) >= 0) bat_present = v;
    snprintf(path, sizeof(path), "%s/internal_resistance", SYSFS); if ((v = read_int(path)) >= 0) internal_resistance = v;
    snprintf(path, sizeof(path), "%s/charge_now", SYSFS); if ((v = read_int(path)) >= 0) charge_now = v;
    snprintf(path, sizeof(path), "%s/charge_term_current", SYSFS); if ((v = read_int(path)) >= 0) charge_term_current = v;
    snprintf(path, sizeof(path), "%s/constant_charge_current_max", SYSFS); if ((v = read_int(path)) >= 0) constant_charge_current_max = v;
    snprintf(path, sizeof(path), "%s/constant_charge_voltage_max", SYSFS); if ((v = read_int(path)) >= 0) constant_charge_voltage_max = v;
    snprintf(path, sizeof(path), "%s/temp_max", SYSFS); if ((v = read_int(path)) >= 0) temp_max = v;
    snprintf(path, sizeof(path), "%s/temp_min", SYSFS); if ((v = read_int(path)) >= 0) temp_min = v;
    snprintf(path, sizeof(path), "%s/charger_temp", SYSFS); if ((v = read_int(path)) >= 0) charger_temp = v;
    snprintf(path, sizeof(path), "%s/charge_done", SYSFS); if ((v = read_int(path)) >= 0) charge_done = v;
    snprintf(path, sizeof(path), "%s/input_voltage_settled", SYSFS); if ((v = read_int(path)) >= 0) input_voltage_settled = v;
    snprintf(path, sizeof(path), "%s/safety_timer_expired", SYSFS); if ((v = read_int(path)) >= 0) safety_timer_expired = v;
    snprintf(path, sizeof(path), "%s/calibrate", SYSFS); if ((v = read_int(path)) >= 0) calibrate = v;
    snprintf(path, sizeof(path), "%s/temp_ambient", SYSFS); if ((v = read_int(path)) >= 0) temp_ambient = v;
    /* 系统信息（复用 30s 缓存）*/
    {
        FILE *mf = fopen("/proc/meminfo", "r");
        if (mf) { char ml[256]; while (fgets(ml, sizeof(ml), mf)) {
            if (sscanf(ml, "MemTotal: %d kB", &v) == 1) mem_total = v;
            else if (sscanf(ml, "MemAvailable: %d kB", &v) == 1) mem_avail = v;
            else if (sscanf(ml, "MemFree: %d kB", &v) == 1) mem_free = v;
        } fclose(mf); }
        struct statfs sf;
        if (statfs("/data", &sf) == 0) {
            storage_total = (int)((long long)sf.f_blocks * sf.f_bsize / (1024*1024));
            storage_free = (int)((long long)sf.f_bfree * sf.f_bsize / (1024*1024));
        }
        FILE *uf = fopen("/proc/uptime", "r");
        if (uf) { double up; if (fscanf(uf, "%lf", &up) == 1) uptime_secs = (int)up; fclose(uf); }
        FILE *cf = fopen("/sys/devices/system/cpu/present", "r");
        if (cf) { int a,b; if (fscanf(cf, "%d-%d", &a, &b) == 2) cpu_cores = b-a+1; fclose(cf); }
        if (cpu_cores < 0) cpu_cores = 1;
        FILE *vf = fopen("/proc/version", "r");
        if (vf) { if (fgets(kernel_ver, sizeof(kernel_ver), vf)) {
            char *sp = strstr(kernel_ver, "version "); if (sp) memmove(kernel_ver, sp+8, strlen(sp)-7);
            char *nl = strchr(kernel_ver, '\n'); if (nl) *nl = 0;
        } fclose(vf); }
        /* CPU 型号：ARM 在 "Hardware" 行，x86 在 "model name" 行（v1.6.0 补兼容） */
        FILE *cif = fopen("/proc/cpuinfo", "r");
        if (cif) {
            char cl[256];
            while (fgets(cl, sizeof(cl), cif)) {
                char *p = strstr(cl, "Hardware");
                if (!p) p = strstr(cl, "model name");
                if (p) {
                    char *c = strchr(p, ':');
                    if (c) {
                        c++;
                        while (*c == ' ') c++;
                        char *nl = strchr(c, '\n'); if (nl) *nl = 0;
                        snprintf(cpu_model, sizeof(cpu_model), "%s", c);
                        break;
                    }
                }
            }
            fclose(cif);
        }
        /* 每核频率兜底：部分平台没有 cpufreq，读 scaling_cur_freq 失败时试 cpuinfo_cur_freq */
        if (cpu_freq_khz[0] == 0) {
            int v2 = read_int("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_cur_freq");
            if (v2 > 0) cpu_freq_khz[0] = v2;
        }
        FILE *dmf = fopen("/proc/device-tree/model", "r");
        if (dmf) { if (fgets(device_model, sizeof(device_model), dmf)) {
            char *nl = strchr(device_model, '\n'); if (nl) *nl = 0;
        } fclose(dmf); }
        ip_addr[0] = 0; wifi_rssi = -128; wifi_on = 0; mobile_on = 0;
        FILE *nf = fopen("/proc/net/route", "r");
        if (nf) { char nl[256]; while (fgets(nl, sizeof(nl), nf)) {
            char ifn[32]; unsigned dest; int met;
            if (sscanf(nl, "%31s %X %*s %*d %*d %*d %*d %d", ifn, &dest, &met) >= 3 && dest == 0 && met == 0) strncpy(ip_addr, ifn, 63);
        } fclose(nf); }
        FILE *wf = fopen("/proc/net/wireless", "r");
        if (wf) { char wl[256]; while (fgets(wl, sizeof(wl), wf)) {
            char ifn[32]; int lk, lv;
            if (sscanf(wl, "%31s: %*d %d. %d.", ifn, &lk, &lv) >= 3) { wifi_rssi = lv; wifi_on = 1; }
        } fclose(wf); }
        FILE *mif = fopen("/proc/net/dev", "r");
        if (mif) { char ml[256]; while (fgets(ml, sizeof(ml), mif)) {
            if (strstr(ml, "rmnet") || strstr(ml, "ccmni") || strstr(ml, "wwan")) { mobile_on = 1; break; }
        } fclose(mif); }
    }
}

/* ★ 新增：读取 PD 协议真值 + MTK 原生能力（不写，仅探测） */
static void read_pd_and_mtk(void) {
    char buf[64];
    /* PD 能力档位 pdo1..pdo7（原始 hex，解析出最高电压/电流） */
    pd_pdo_count = 0; pd_pdo_max_mv = 0; pd_pdo_max_ma = 0;
    for (int i = 1; i <= 7; i++) {
        char path[128];
        snprintf(path, sizeof(path), "/sys/class/Charging_Adapter/pd_adapter/pdo%d", i);
        if (read_str(path, buf, sizeof(buf)) <= 0) continue;
        unsigned int v = (unsigned int)strtoul(buf, NULL, 16);
        if (v == 0) continue;
        pd_pdo_count++;
        /* PDO 解析（USB-PD 规范）：固定电压 PDO 的 bits 9:0 电压(50mV/LSB), bits 19:10 电流(10mA/LSB) */
        unsigned int mv = (v & 0x3FF) * 50;
        unsigned int ma = ((v >> 10) & 0x3FF) * 10;
        if ((int)mv > pd_pdo_max_mv) { pd_pdo_max_mv = mv; pd_pdo_max_ma = ma; }
    }
    if (read_str("/sys/class/Charging_Adapter/pd_adapter/adapter_svid", pd_svid, sizeof(pd_svid)) <= 0)
        strcpy(pd_svid, "");
    if (read_str("/sys/class/Charging_Adapter/pd_adapter/current_state", pd_state, sizeof(pd_state)) <= 0)
        strcpy(pd_state, "");
    /* MTK JEITA 温控档位 */
    int v = read_int("/sys/class/power_supply/usb/jeita_chg_index");
    jeita_index = v;
    /* MTK 原生电量上限 charge_control_limit（只读探测） */
    v = read_int("/sys/class/power_supply/battery/charge_control_limit");
    if (v >= 0) { cc_limit_supported = 1; cc_limit_val = v; }
    /* MTK 电源路径 en_power_path（旁路的另一种实现，只读探测） */
    v = read_int("/proc/mtk_battery_cmd/en_power_path");
    if (v >= 0) power_path_en = v;
    /* ★ SOH 变化追踪（首次记录后对比） */
    if (soh_native > 0) {
        if (soh_first < 0) {
            soh_first = soh_native;
            soh_delta_x10 = 0;
        } else {
            soh_delta_x10 = (soh_native - soh_first) * 10;
        }
    }
}

static void update_temps(void) {
    time_t tn = time(NULL);
    if (tn - temps_last < 5) return;  /* 5s 节流 */
    temps_last = tn;
    soc_temp = -1; gpu_temp = -1; chg_temp = -1; case_temp = -1;
    /* 扫描上限从 20 扩到 96：MTK/高通多平台关键温度常位于 zone34+ (GPU/充电IC/机身) */
    for (int i = 0; i < 96; i++) {
        char tzpath[128]; snprintf(tzpath, sizeof(tzpath), "%s/thermal_zone%d/type", THERMAL, i);
        char tname[64];
        if (read_str(tzpath, tname, sizeof(tname)) <= 0) continue;
        snprintf(tzpath, sizeof(tzpath), "%s/thermal_zone%d/temp", THERMAL, i);
        int tval = read_int(tzpath);
        if (tval < 0) continue;
        /* 值域过滤：剔除未初始化传感器的垃圾值
         * 实测 MTK 上 vtskin / camera 系列常报 -274000，cp 系列报 300，mtk-charger 报 0
         * 允许 -30.000C ~ 150.000C */
        if (tval < -30000 || tval > 150000) continue;
        if (soc_temp < 0 && (strstr(tname, "soc_max") || strstr(tname, "tsens") || strstr(tname, "cpu-therm") || strstr(tname, "cpu_max") || strstr(tname, "cpuss") || strstr(tname, "cpu_therm") || strstr(tname, "soc_therm")))
            soc_temp = tval / 1000;
        else if (gpu_temp < 0 && (strstr(tname, "gpu1") || strstr(tname, "gpu-therm") || strstr(tname, "gpu_therm") || strstr(tname, "gpu-max") || strstr(tname, "gpu_max") || strstr(tname, "gpu-thermal") || strstr(tname, "gpu_thermal")))
            gpu_temp = tval / 1000;
        else if (chg_temp < 0 && (strstr(tname, "mtk-master-charger") || strstr(tname, "usb-therm") || strstr(tname, "bq") || strstr(tname, "smb") || strstr(tname, "charger-thermal") || strstr(tname, "charger_thermal") || strstr(tname, "charger1_therm") || strstr(tname, "charger2_therm")))
            chg_temp = tval / 1000;
        else if (case_temp < 0 && (strstr(tname, "X7_therm") || strstr(tname, "case") || strstr(tname, "skin") || strstr(tname, "quiet_therm") || strstr(tname, "shell") || strstr(tname, "frame") || strstr(tname, "back_therm")))
            case_temp = tval / 1000;
    }
    /* ★ 充电IC 温度优先直读 power_supply（比遍历 thermal 快且准，实测 MTK: 670=67°C） */
    if (chg_temp <= 0) {
        int v = read_int("/sys/class/power_supply/mtk-master-charger/temp");
        if (v > 0) chg_temp = v / 10;
    }
    if (chg_temp <= 0) {
        int v = read_int(SYSFS "/charger_temp");
        if (v > 0) chg_temp = v / 10;
    }
    /* ★ 机身温度兜底：读 MTK 的 X7_therm 已含，若仍空则试 battery 附近的 thermal */
    if (case_temp <= 0) {
        int v = read_int(SYSFS "/temp");
        if (v > 0) case_temp = v / 10;   /* 电池温度兜底 */
    }
}

static void handle_charging_state(void);
static void send_resp(int fd, int code, const char *ctype, const char *body);  /* v1.5.0 前向声明 */
static void cal_log(const char *msg);                                          /* v1.5.0 前向声明 */
/* ═══════════════════════════════════════════════════════════
   v1.6.0 系统信息采集（供 App 走模块取数，替代 App 自读 sysfs）
   全部字段来自 /proc 与 /sys，root 权限下可见性优于 App。
   ═══════════════════════════════════════════════════════════ */

/* CPU 每核频率 + 架构 + 缓存（30s 节流） */
static void update_cpu_detail(void) {
    time_t tn = time(NULL);
    static time_t last = 0;
    if (tn - last < 30) return;
    last = tn;

    /* 每核频率 */
    int found_max = 0;
    for (int i = 0; i < MAX_CORES && i < 32; i++) {
        char pth[160];
        snprintf(pth, sizeof(pth), "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq", i);
        int v = read_int(pth);
        if (v > 0) cpu_freq_khz[i] = v;
        snprintf(pth, sizeof(pth), "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", i);
        if ((v = read_int(pth)) > 0) { cpu_freq_max_khz[i] = v; found_max = 1; }
        snprintf(pth, sizeof(pth), "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_min_freq", i);
        if ((v = read_int(pth)) > 0) cpu_freq_min_khz[i] = v;
    }
    (void)found_max;

    /* 架构与 ABI
       ★ v1.10.1 真机修复：/proc/cpuinfo 的 "CPU architecture" 给的是
       内核内部编号（ARMv8 报 8、ARMv7 报 7），不是字符串。
       原先直接照抄 → App 显示 "8"。现在做映射。 */
    cpu_arch[0] = 0;
    read_str("/proc/sys/kernel/arch", cpu_arch, sizeof(cpu_arch));
    if (cpu_arch[0] == 0 || (cpu_arch[0] >= '0' && cpu_arch[0] <= '9')) {
        int arch_num = cpu_arch[0] ? atoi(cpu_arch) : 0;
        FILE *f = fopen("/proc/cpuinfo", "r");
        if (f) {
            char l[256];
            while (fgets(l, sizeof(l), f)) {
                char *p = strstr(l, "CPU architecture");
                if (p) {
                    char *c = strchr(p, ':');
                    if (c) { c++; while (*c == ' ') c++; arch_num = atoi(c); }
                    break;
                }
            }
            fclose(f);
        }
        /* ARM 内核编号 → 可读名（含 64 位判断） */
        if (arch_num >= 8) {
            snprintf(cpu_arch, sizeof(cpu_arch), "ARMv%d-A (aarch64)", arch_num);
        } else if (arch_num >= 7) {
            snprintf(cpu_arch, sizeof(cpu_arch), "ARMv%d-A (armv7l)", arch_num);
        } else if (arch_num > 0) {
            snprintf(cpu_arch, sizeof(cpu_arch), "ARMv%d", arch_num);
        } else {
            snprintf(cpu_arch, sizeof(cpu_arch), "arm64");
        }
    }

    /* 缓存（ARM 上是 indexN/size，x86 是 cpu0/cache/indexN/size） */
    FILE *f = fopen("/sys/devices/system/cpu/cpu0/cache/index0/size", "r");
    if (f) { if (fgets(cpu_cache_l1, sizeof(cpu_cache_l1), f)) { char *nl = strchr(cpu_cache_l1,'\n'); if (nl) *nl = 0; } fclose(f); }
    f = fopen("/sys/devices/system/cpu/cpu0/cache/index2/size", "r");
    if (f) { if (fgets(cpu_cache_l2, sizeof(cpu_cache_l2), f)) { char *nl = strchr(cpu_cache_l2,'\n'); if (nl) *nl = 0; } fclose(f); }
    f = fopen("/sys/devices/system/cpu/cpu0/cache/index3/size", "r");
    if (f) { if (fgets(cpu_cache_l3, sizeof(cpu_cache_l3), f)) { char *nl = strchr(cpu_cache_l3,'\n'); if (nl) *nl = 0; } fclose(f); }
}

/* 整机 CPU 使用率（两次 /proc/stat 差值）+ idle 占比 */
static void update_cpu_usage(void) {
    FILE *f = fopen("/proc/stat", "r");
    if (!f) return;
    char line[256];
    if (fgets(line, sizeof(line), f)) {
        long u = 0, n = 0, sy = 0, idle = 0, iow = 0, irq = 0, sirq = 0, steal = 0;
        int got = sscanf(line, "cpu  %ld %ld %ld %ld %ld %ld %ld %ld",
                         &u, &n, &sy, &idle, &iow, &irq, &sirq, &steal);
        if (got >= 5) {
            long total = u + n + sy + idle + iow + irq + sirq + steal;
            if (cpu_ticks_prev_total > 0) {
                long dt = total - cpu_ticks_prev_total;
                long di = idle - cpu_ticks_prev_idle;
                if (dt > 0) {
                    cpu_usage_total = (int)((dt - di) * 1000 / dt);   /* ×10 */
                    cpu_idle_pct    = (int)(di * 1000 / dt);
                    /* 深度睡眠：iowait + idle 中属于深睡的部分无法直接测，
                       这里用 idle 与 iowait 之和近似"总空闲"，深睡单独尝试 cpuidle */
                }
            }
            cpu_ticks_prev_total = total;
            cpu_ticks_prev_idle  = idle;
        }
    }
    fclose(f);

    /* 深度睡眠占比：读 cpuidle 各 state 的累计时间 */
    static long sleep_prev = 0, sleep_total_prev = 0;
    long sleep_sum = 0;
    for (int c = 0; c < MAX_CORES && c < 8; c++) {
        char pth[160];
        snprintf(pth, sizeof(pth), "/sys/devices/system/cpu/cpu%d/cpuidle", c);
        if (access(pth, R_OK) != 0) break;
        for (int st = 2; st < 6; st++) {   /* state0/1 是浅睡，从 2 起算深睡 */
            char p2[192];
            snprintf(p2, sizeof(p2), "%s/state%d/time", pth, st);
            int v = read_int(p2);
            if (v > 0) sleep_sum += v;
        }
    }
    if (sleep_sum > 0) {
        FILE *up = fopen("/proc/uptime", "r");
        double up_s = 0;
        if (up) { if (fscanf(up, "%lf", &up_s) == 1) {} fclose(up); }
        long now_total = (long)(up_s * 1000000);   /* 微秒 */
        if (sleep_total_prev > 0) {
            long ds = sleep_sum - sleep_prev;
            long dtot = now_total - sleep_total_prev;
            if (dtot > 0) cpu_deep_sleep_pct = (int)(ds * 1000 / dtot);
        }
        sleep_prev = sleep_sum;
        sleep_total_prev = now_total;
    }
}

/* 内存细分 + swap + zram */
static void update_mem_detail(void) {
    FILE *f = fopen("/proc/meminfo", "r");
    if (f) {
        char l[256]; int v;
        while (fgets(l, sizeof(l), f)) {
            if (sscanf(l, "MemTotal: %d kB", &v) == 1) mem_total = v;
            else if (sscanf(l, "MemAvailable: %d kB", &v) == 1) mem_avail = v;
            else if (sscanf(l, "MemFree: %d kB", &v) == 1) mem_free = v;
            else if (sscanf(l, "Buffers: %d kB", &v) == 1) mem_buffers = v;
            else if (sscanf(l, "Cached: %d kB", &v) == 1) mem_cached = v;
            else if (sscanf(l, "SReclaimable: %d kB", &v) == 1) mem_sreclaim = v;
            else if (sscanf(l, "SUnreclaim: %d kB", &v) == 1) mem_sunreclaim = v;
            else if (sscanf(l, "Shmem: %d kB", &v) == 1) mem_shmem = v;
            else if (sscanf(l, "PageTables: %d kB", &v) == 1) mem_pagetables = v;
            else if (sscanf(l, "KernelStack: %d kB", &v) == 1) mem_kernelstack = v;
            else if (sscanf(l, "VmallocUsed: %d kB", &v) == 1) mem_vmallocused = v;
            else if (sscanf(l, "SwapTotal: %d kB", &v) == 1) mem_swap_total = v;
            else if (sscanf(l, "SwapFree: %d kB", &v) == 1) mem_swap_free = v;
            else if (sscanf(l, "Slab: %d kB", &v) == 1) mem_slab = v;
            else if (sscanf(l, "Mapped: %d kB", &v) == 1) mem_mapped = v;
        }
        fclose(f);
    }
    /* zram：取第一个 zram 块设备的 mm_stat */
    FILE *z = fopen("/sys/block/zram0/mm_stat", "r");
    if (z) {
        long orig = 0, compr = 0, mem_used = 0;
        if (fscanf(z, "%ld %ld %ld", &orig, &compr, &mem_used) == 3) {
            mem_zram_orig = (int)(orig / 1024);
            mem_zram_compr = (int)(compr / 1024);
            mem_zram_used = (int)(mem_used / 1024);
        }
        fclose(z);
    }
}

/* 存储分区（App 的"存储"页需要） */
static void update_partitions(void) {
    static const char *cands[] = {"/data", "/system", "/cache", "/vendor", "/metadata", "/storage/emulated/0", NULL};
    part_count = 0;
    for (int i = 0; cands[i] && part_count < MAX_PARTS; i++) {
        struct statfs sf;
        if (statfs(cands[i], &sf) != 0) continue;
        snprintf(part_mount[part_count], sizeof(part_mount[0]), "%s", cands[i]);
        part_total_mb[part_count] = (long)((long long)sf.f_blocks * sf.f_bsize / (1024*1024));
        part_free_mb[part_count]  = (long)((long long)sf.f_bfree  * sf.f_bsize / (1024*1024));
        part_count++;
    }
}

/* ═══════════════════════════════════════════════════════════
   v1.6.0 GPU 采集
   覆盖四类平台（与 App 的 GpuDataSource 探测路径对齐）：
     1. 高通 Adreno: /sys/class/kgsl/kgsl-3d0/
     2. ARM Mali:    /sys/class/devfreq/ 里的 mali 项 或 /proc/mali/
     3. 联发科:      /proc/gpufreq/ 或 /sys/kernel/ged/
     4. 通用:        扫描 /sys/class/devfreq/ 全部条目
   ═══════════════════════════════════════════════════════════ */

/* 判断 devfreq 目录名像不像 GPU */
static int looks_like_gpu(const char *name) {
    if (!name) return 0;
    return strstr(name, "mali") || strstr(name, "gpu") || strstr(name, "kgsl")
        || strstr(name, "gpufreq") || strstr(name, "ged") || strstr(name, "adreno");
}

/* 从指定 devfreq 目录读全套参数 */
static int gpu_from_devfreq(const char *dir) {
    char pth[256];
    int got = 0;

    snprintf(pth, sizeof(pth), "/sys/class/devfreq/%s/cur_freq", dir);
    int v = read_int(pth);
    if (v > 0) { gpu_freq_khz = v / 1000; got = 1; }

    snprintf(pth, sizeof(pth), "/sys/class/devfreq/%s/max_freq", dir);
    if ((v = read_int(pth)) > 0) gpu_freq_max_khz = v / 1000;

    snprintf(pth, sizeof(pth), "/sys/class/devfreq/%s/min_freq", dir);
    if ((v = read_int(pth)) > 0) gpu_freq_min_khz = v / 1000;

    snprintf(pth, sizeof(pth), "/sys/class/devfreq/%s/governor", dir);
    if (read_str(pth, gpu_governor, sizeof(gpu_governor)) > 0) { /* ok */ }

    snprintf(pth, sizeof(pth), "/sys/class/devfreq/%s/available_governors", dir);
    if (read_str(pth, gpu_avail_gov, sizeof(gpu_avail_gov)) > 0) { /* ok */ }

    /* load：不同平台格式不同，可能是 "x y"（busy/total）或百分比 */
    snprintf(pth, sizeof(pth), "/sys/class/devfreq/%s/load", dir);
    FILE *f = fopen(pth, "r");
    if (f) {
        long a = 0, b = 0;
        if (fscanf(f, "%ld %ld", &a, &b) == 2 && b > 0) {
            gpu_load_pct = (int)(a * 1000 / b);
            snprintf(gpu_load_src, sizeof(gpu_load_src), "devfreq");
        } else if (fscanf(f, "%ld", &a) == 1 && a >= 0 && a <= 100) {
            gpu_load_pct = (int)(a * 10);
            snprintf(gpu_load_src, sizeof(gpu_load_src), "devfreq");
        }
        fclose(f);
    }
    return got;
}

static void update_gpu(void) {
    time_t tn = time(NULL);
    static time_t last = 0;
    if (tn - last < 5) return;   /* 5s 刷新（频率/负载变化快） */
    last = tn;

    /* ── 1. 高通 Adreno（kgsl）── */
    {
        const char *bases[] = {
            "/sys/class/kgsl/kgsl-3d0",
            "/sys/devices/platform/kgsl-3d0.0/kgsl/kgsl-3d0",
            "/sys/devices/soc/1c00000.qcom,kgsl-3d0/kgsl/kgsl-3d0",
            "/sys/devices/virtual/kgsl/kgsl-3d0",
            NULL
        };
        for (int i = 0; bases[i]; i++) {
            char pth[256];
            snprintf(pth, sizeof(pth), "%s/gpuclk", bases[i]);
            int v = read_int(pth);
            if (v > 0) {
                gpu_freq_khz = v / 1000000;   /* Hz → kHz */
                snprintf(gpu_model, sizeof(gpu_model), "Adreno");
                snprintf(gpu_vendor, sizeof(gpu_vendor), "Qualcomm");
                break;
            }
        }
        if (gpu_freq_khz < 0) {
            /* devfreq 子路径 */
            for (int i = 0; bases[i]; i++) {
                char pth[256];
                snprintf(pth, sizeof(pth), "%s/devfreq/cur_freq", bases[i]);
                int v = read_int(pth);
                if (v > 0) {
                    gpu_freq_khz = v / 1000;
                    snprintf(gpu_model, sizeof(gpu_model), "Adreno");
                    snprintf(gpu_vendor, sizeof(gpu_vendor), "Qualcomm");
                    snprintf(pth, sizeof(pth), "%s/devfreq/max_freq", bases[i]);
                    if ((v = read_int(pth)) > 0) gpu_freq_max_khz = v / 1000;
                    snprintf(pth, sizeof(pth), "%s/devfreq/governor", bases[i]);
                    read_str(pth, gpu_governor, sizeof(gpu_governor));
                    break;
                }
            }
        }
        /* 负载：kgsl 的 gpu_busy_percentage 最准 */
        if (gpu_load_pct < 0) {
            for (int i = 0; bases[i]; i++) {
                char pth[256];
                snprintf(pth, sizeof(pth), "%s/gpu_busy_percentage", bases[i]);
                int v = read_int(pth);
                if (v >= 0 && v <= 100) {
                    gpu_load_pct = v * 10;
                    snprintf(gpu_load_src, sizeof(gpu_load_src), "kgsl");
                    break;
                }
                /* gpubusy: "busy total" */
                snprintf(pth, sizeof(pth), "%s/gpubusy", bases[i]);
                FILE *f = fopen(pth, "r");
                if (f) {
                    long a = 0, b = 0;
                    if (fscanf(f, "%ld %ld", &a, &b) == 2 && b > 0) {
                        gpu_load_pct = (int)(a * 1000 / b);
                        snprintf(gpu_load_src, sizeof(gpu_load_src), "kgsl");
                        fclose(f);
                        break;
                    }
                    fclose(f);
                }
            }
        }
    }

    /* ── 2. Mali ── */
    if (gpu_freq_khz < 0) {
        /* /sys/class/misc/mali0/device/gpuinfo 给型号 */
        char buf[128];
        if (read_str("/sys/class/misc/mali0/device/gpuinfo", buf, sizeof(buf)) > 0) {
            snprintf(gpu_model, sizeof(gpu_model), "%s", buf);
            snprintf(gpu_vendor, sizeof(gpu_vendor), "ARM");
        }
        if (gpu_model[0] == 0) {
            if (read_str("/proc/mali/gpu_clock", buf, sizeof(buf)) > 0) {
                snprintf(gpu_model, sizeof(gpu_model), "Mali");
                snprintf(gpu_vendor, sizeof(gpu_vendor), "ARM");
            }
        }
    }

    /* ── 3. MTK ── */
    if (gpu_freq_khz < 0) {
        int v = read_int("/proc/gpufreq/gpufreq_var");
        if (v > 0) { gpu_freq_khz = v / 1000; }
        if (gpu_freq_khz < 0 && read_int("/sys/module/ged/parameters/gpu_freq") > 0) {
            gpu_freq_khz = read_int("/sys/module/ged/parameters/gpu_freq") / 1000;
        }
        if (gpu_freq_khz < 0) {
            v = read_int("/proc/gpufreq/gpufreq_opp_freq");
            if (v > 0) gpu_freq_khz = v / 1000;
        }
    }

    /* ── 4. 通用 devfreq 扫描（最后兜底）── */
    if (gpu_freq_khz < 0) {
        DIR *d = opendir("/sys/class/devfreq");
        if (d) {
            struct dirent *e;
            while ((e = readdir(d))) {
                if (e->d_name[0] == '.') continue;
                if (!looks_like_gpu(e->d_name)) continue;
                /* 限制目录名长度，避免 snprintf 截断告警（devfreq 名通常 < 32 字符） */
                char dname[64];
                snprintf(dname, sizeof(dname), "%.63s", e->d_name);
                if (gpu_from_devfreq(dname)) {
                    if (gpu_model[0] == 0) snprintf(gpu_model, sizeof(gpu_model), "%.127s", e->d_name);
                    break;
                }
            }
            closedir(d);
        }
    }

    /* 型号/厂商标注（若前面没识别出）*/
    if (gpu_model[0] == 0) {
        char buf[128];
        if (read_str("/sys/kernel/gpu/gpu_model", buf, sizeof(buf)) > 0)
            snprintf(gpu_model, sizeof(gpu_model), "%s", buf);
    }

    /* 节流判定：当前频率 < 最大频率的 70% */
    gpu_throttled = (gpu_freq_khz > 0 && gpu_freq_max_khz > 0
                     && gpu_freq_khz < gpu_freq_max_khz * 70 / 100) ? 1 : 0;
}

/* ═══════════════════════════════════════════════════════════
   v1.7.0 OEM 属性采集（供 App「OEM/系统」页走模块取数）

   来源优先级：
     1. /system|vendor|product|odm/build.prop（静态）
     2. getprop <key>（能拿 persist.* 等运行时属性）
   模块以 root 运行，getprop 可用；相比 App 调 SystemProperties.get()，
   走模块的好处是多设备一致 + App 不需要自己读系统属性。
   ═══════════════════════════════════════════════════════════ */

/* 用 getprop 读单个属性 */
static int prop_get(const char *key, char *out, int outsz) {
    const char *gp = "/system/bin/getprop";
    if (access(gp, X_OK) != 0) {
        gp = "/vendor/bin/getprop";
        if (access(gp, X_OK) != 0) return -1;
    }
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "%s '%s' 2>/dev/null", gp, key);
    FILE *f = popen(cmd, "r");
    if (!f) return -1;
    out[0] = 0;
    if (fgets(out, outsz, f)) {
        char *nl = strchr(out, '\n'); if (nl) *nl = 0;
    }
    pclose(f);
    return out[0] ? (int)strlen(out) : -1;
}

/* ═══ v1.8.0 设备详情采集（5 分钟缓存）═══ */
static void update_device_detail(void) {
    time_t tn = time(NULL);
    static time_t last = 0;
    if (tn - last < 300) return;
    last = tn;

    char b[256];
    /* 便捷宏：试多个 key，第一个非空即用 */
    #define TRY_PROP(dst, ...) do { \
        const char *ks[] = {__VA_ARGS__, NULL}; \
        for (int _i = 0; ks[_i]; _i++) { \
            if (prop_get(ks[_i], b, sizeof(b)) > 0 && b[0]) { \
                snprintf(dst, sizeof(dst), "%.*s", (int)sizeof(dst)-1, b); break; } \
        } } while (0)

    #define TRY_INT(dst, ...) do { \
        const char *ks[] = {__VA_ARGS__, NULL}; \
        for (int _i = 0; ks[_i]; _i++) { \
            if (prop_get(ks[_i], b, sizeof(b)) > 0 && b[0]) { \
                dst = atoi(b); break; } \
        } } while (0)

    /* ── 存储 / 内存 ── */
    TRY_PROP(dev_storage_type, "ro.boot.storage_type", "ro.vendor.storage_type", "ro.boot.hwstorage");
    /* ★ v1.10.1 真机修复：小米等机型没有 storage_type 属性，
       但 /sys/block 下的 device/model 有闪存型号（如 KLUFG8RHHD = 三星 UFS）。
       按型号前缀反查。 */
    if (dev_storage_type[0] == 0) {
        const char *blk[] = {"/sys/block/sda/device/model", "/sys/block/mmcblk0/device/name",
                             "/sys/block/sdb/device/model", NULL};
        char mdl[64];
        for (int bi = 0; blk[bi] && dev_storage_type[0] == 0; bi++) {
            if (read_str(blk[bi], mdl, sizeof(mdl)) <= 0) continue;
            /* KLU/KLU = Samsung UFS, KM = Samsung eMMC, H9T/H9Q = SK Hynix */
            if (strncmp(mdl, "KLU", 3) == 0 || strncmp(mdl, "KLM", 3) == 0)
                snprintf(dev_storage_type, sizeof(dev_storage_type), "UFS (Samsung %.20s)", mdl);
            else if (strncmp(mdl, "KM", 2) == 0)
                snprintf(dev_storage_type, sizeof(dev_storage_type), "eMMC (Samsung %.20s)", mdl);
            else if (strncmp(mdl, "H9T", 3) == 0 || strncmp(mdl, "H9Q", 3) == 0)
                snprintf(dev_storage_type, sizeof(dev_storage_type), "UFS (SK Hynix %.20s)", mdl);
            else if (strncmp(mdl, "Y0", 2) == 0 || strncmp(mdl, "YC", 2) == 0)
                snprintf(dev_storage_type, sizeof(dev_storage_type), "UFS (YMTC %.20s)", mdl);
            else if (mdl[0])
                snprintf(dev_storage_type, sizeof(dev_storage_type), "%.*s", (int)sizeof(dev_storage_type)-1, mdl);
        }
    }
    TRY_PROP(dev_memory_type,  "ro.boot.ddr_type", "ro.ddr_type", "ro.vendor.ddr_type");
    TRY_INT (dev_memory_speed_mhz, "ro.boot.ddr_freq");

    /* ── SoC ── */
    TRY_PROP(dev_soc_chipname, "ro.soc.model", "ro.chipset", "ro.board.chipname", "ro.chipname", "ro.hardware.chipname");
    TRY_PROP(dev_soc_process_node, "ro.soc.process_node", "ro.board.process_node");

    /* ── USB ── */
    TRY_PROP(dev_usb_version, "ro.boot.usb_speed", "ro.usb.speed", "persist.sys.usb.speed");
    TRY_INT (dev_usb_typec, "ro.hardware.usb.typec");

    /* ── 蓝牙 / WiFi ── */
    TRY_PROP(dev_bluetooth_ver, "ro.vendor.bt.version", "ro.bluetooth.version", "ro.boot.bt.version", "persist.vendor.bt.version");
    TRY_INT (dev_wifi_6e, "ro.vendor.wifi.6e_support", "ro.boot.wifi.6e");
    TRY_INT (dev_wifi_ax, "ro.vendor.wifi.ax", "ro.boot.wifi.ax");
    TRY_INT (dev_wifi_be, "ro.vendor.wifi.be_support", "ro.boot.wifi.be");
    if (dev_wifi_be == 1)      snprintf(dev_wifi_standard, sizeof(dev_wifi_standard), "Wi-Fi 7");
    else if (dev_wifi_6e == 1) snprintf(dev_wifi_standard, sizeof(dev_wifi_standard), "Wi-Fi 6E");
    else if (dev_wifi_ax == 1) snprintf(dev_wifi_standard, sizeof(dev_wifi_standard), "Wi-Fi 6");

    /* ── 显示 ── */
    TRY_PROP(dev_display_type, "ro.vendor.display.type", "ro.display.type");
    TRY_PROP(dev_display_bit, "ro.vendor.display.bit");
    TRY_PROP(dev_display_gamut, "ro.vendor.display.gamut", "ro.vendor.display.color_mode");
    TRY_PROP(dev_display_series, "ro.display.series");

    /* ── 安全 ── */
    TRY_PROP(dev_selinux, "ro.build.selinux");
    TRY_PROP(dev_crypto_state, "ro.crypto.state");
    TRY_PROP(dev_crypto_type, "ro.crypto.type");
    TRY_PROP(dev_tee_version, "ro.tee.version");
    TRY_INT (dev_bootloader_locked, "ro.boot.flash.locked");
    TRY_PROP(dev_openssl, "ro.openssl.version", "ro.vendor.openssl.version");
    /* TEE：有个版本号或有 trustedcore 即视为支持 */
    dev_tee_supported = (dev_tee_version[0] != 0
        || access("/dev/tee0", F_OK) == 0
        || access("/vendor/lib64/libtrustonic", F_OK) == 0) ? 1 : 0;

    /* ── 图形 ── */
    TRY_PROP(dev_gles_version, "ro.gles.version", "ro.hardware.egl");
    TRY_PROP(dev_vulkan_level, "ro.vulkan.api.level", "ro.hardware.vulkan");
    TRY_PROP(dev_hardware_egl, "ro.hardware.egl");

    /* ── 序列号 ── */
    TRY_PROP(dev_serial, "ro.serialno", "ro.boot.serialno");

    /* ── 音频 ── */
    TRY_PROP(dev_audio_hires, "ro.vendor.audio.hi_res", "ro.vendor.audio.hires", "ro.audio.hi_res", "ro.boot.audio.hi_res");

    /* ── SIM 双卡 ── */
    TRY_INT(dev_dual_sim, "ro.telephony.sim.count", "persist.radio.multisim.config");
    if (dev_dual_sim > 1) dev_dual_sim = 1;
    TRY_PROP(dev_sim_operator, "gsm.sim.operator.alpha", "gsm.operator.alpha");

    /* ── 硬件能力：优先 /sys、fallback getprop ── */
    /* NFC */
    if (access("/dev/nfc0", F_OK) == 0 || access("/dev/nfc", F_OK) == 0) dev_has_nfc = 1;
    else {
        if (prop_get("ro.boot.nfc", b, sizeof(b)) > 0 && b[0]) dev_has_nfc = atoi(b) ? 1 : 0;
        else if (prop_get("ro.vendor.nfc", b, sizeof(b)) > 0 && b[0]) dev_has_nfc = atoi(b) ? 1 : 0;
        else dev_has_nfc = 0;
    }
    /* 红外：小米用 /sys/class/consumerir */
    if (access("/sys/class/consumerir", F_OK) == 0) dev_has_ir = 1;
    else {
        dev_has_ir = 0;
        const char *ks[] = {"ro.consumerir.support", "ro.vendor.consumerir", NULL};
        for (int i = 0; ks[i]; i++)
            if (prop_get(ks[i], b, sizeof(b)) > 0 && b[0]) { dev_has_ir = atoi(b) ? 1 : 0; break; }
    }
    /* UWB */
    dev_has_uwb = (access("/dev/uwb", F_OK) == 0
        || access("/sys/class/uwb0", F_OK) == 0) ? 1 : 0;
    /* 无线充电：读 power_supply 里的 wireless 设备 */
    dev_has_wireless_chg = 0;
    {
        DIR *d = opendir("/sys/class/power_supply");
        if (d) {
            struct dirent *e;
            while ((e = readdir(d))) {
                if (strstr(e->d_name, "wireless") || strstr(e->d_name, "wlchg")) { dev_has_wireless_chg = 1; break; }
            }
            closedir(d);
        }
    }

    #undef TRY_PROP
    #undef TRY_INT
}

/* 屏幕分辨率 / 密度 / 热区统计（变化少，但屏可能旋转，30s 一次） */
static void update_screen_and_thermal(void) {
    time_t tn = time(NULL);
    static time_t last = 0;
    if (tn - last < 30) return;
    last = tn;

    /* ── 屏幕：/sys/class/graphics/fb0/{virtual_size,stride} 或 modes ── */
    char buf[128];
    dev_screen_w = dev_screen_h = -1;
    if (read_str("/sys/class/graphics/fb0/virtual_size", buf, sizeof(buf)) > 0) {
        int w = 0, h = 0;
        if (sscanf(buf, "%d,%d", &w, &h) == 2) { dev_screen_w = w; dev_screen_h = h; }
    }
    if (dev_screen_w < 0) {
        /* 退路：dumpsys（root 可用）取 DisplayDeviceInfo */
        FILE *f = popen("/system/bin/dumpsys display 2>/dev/null | grep -m1 -e real -e deviceWidth", "r");
        if (f) {
            char l[512];
            while (fgets(l, sizeof(l), f)) {
                int w = 0, h = 0;
                char *p = strstr(l, "real ");
                if (p && sscanf(p, "real %dx%d", &w, &h) == 2) { dev_screen_w = w; dev_screen_h = h; break; }
                p = strstr(l, "deviceWidth=");
                if (p && sscanf(p, "deviceWidth=%d deviceHeight=%d", &w, &h) == 2) { dev_screen_w = w; dev_screen_h = h; break; }
                p = strstr(l, "deviceWidth=");
                if (p && sscanf(p, "deviceWidth=%d", &w) == 1) { dev_screen_w = w; }
            }
            pclose(f);
        }
    }
    if (dev_screen_w < 0) {
        /* 最后退路：读 /sys/class/graphics/fb0/modes */
        if (read_str("/sys/class/graphics/fb0/modes", buf, sizeof(buf)) > 0) {
            char *p = strchr(buf, ':');
            if (p) { int w = 0, h = 0; if (sscanf(p + 1, "%dx%d", &w, &h) == 2) { dev_screen_w = w; dev_screen_h = h; } }
        }
    }

    /* 密度：从 build.prop 的 ro.sf.lcd_density */
    FILE *lf = fopen("/system/build.prop", "r");
    if (lf) {
        char l[256];
        while (fgets(l, sizeof(l), lf)) {
            if (strncmp(l, "ro.sf.lcd_density=", 18) == 0) { dev_screen_density = atoi(l + 18); break; }
        }
        fclose(lf);
    }
    if (dev_screen_density < 0) {
        if (prop_get("ro.sf.lcd_density", buf, sizeof(buf)) > 0 && buf[0])
            dev_screen_density = atoi(buf);
    }

    /* ── 热区统计：列出所有 zone 类型 ── */
    dev_thermal_zones = 0;
    dev_thermal_types[0] = 0;
    {
        DIR *d = opendir(THERMAL);
        if (d) {
            struct dirent *e;
            int off = 0;
            while ((e = readdir(d))) {
                if (strncmp(e->d_name, "thermal_zone", 12) != 0) continue;
                char pth[192];
                snprintf(pth, sizeof(pth), "%s/%.150s/type", THERMAL, e->d_name);
                char t[64];
                if (read_str(pth, t, sizeof(t)) <= 0) continue;
                dev_thermal_zones++;
                if (off < (int)sizeof(dev_thermal_types) - 70) {
                    off += snprintf(dev_thermal_types + off, sizeof(dev_thermal_types) - off,
                                    "%s%s", off ? "," : "", t);
                }
            }
            closedir(d);
        }
    }
}

/* ═══ v1.7.0 OEM 采集（5 分钟缓存，都是静态/半静态属性）═══ */
static void update_oem(void) {
    time_t tn = time(NULL);
    static time_t last = 0;
    if (tn - last < 300) return;
    last = tn;

    char buf[256];
    int got_any = 0;

    /* ── 厂商识别 ── */
    if (prop_get("ro.product.brand", buf, sizeof(buf)) > 0 ||
        prop_get("ro.product.manufacturer", buf, sizeof(buf)) > 0) {
        snprintf(oem_brand, sizeof(oem_brand), "%.*s", (int)sizeof(oem_brand)-1, buf);
        got_any = 1;
    }
    /* OS 名称：各家专有属性 */
    if (prop_get("ro.mi.os.version.name", buf, sizeof(buf)) > 0 ||
        prop_get("ro.miui.ui.version.name", buf, sizeof(buf)) > 0) {
        snprintf(oem_os_name, sizeof(oem_os_name), "%.*s", (int)sizeof(oem_os_name)-1, buf);
    } else if (prop_get("ro.build.version.oplusrom", buf, sizeof(buf)) > 0) {
        snprintf(oem_os_name, sizeof(oem_os_name), "ColorOS %.*s", (int)sizeof(oem_os_name)-9, buf);
    } else if (prop_get("ro.vivo.os.version", buf, sizeof(buf)) > 0) {
        snprintf(oem_os_name, sizeof(oem_os_name), "OriginOS %.*s", (int)sizeof(oem_os_name)-10, buf);
    } else if (prop_get("ro.build.version.incremental", buf, sizeof(buf)) > 0) {
        snprintf(oem_os_name, sizeof(oem_os_name), "%.*s", (int)sizeof(oem_os_name)-1, buf);
    }
    if (prop_get("ro.build.display.id", buf, sizeof(buf)) > 0)
        snprintf(oem_os_version, sizeof(oem_os_version), "%.*s", (int)sizeof(oem_os_version)-1, buf);
    if (prop_get("ro.build.fingerprint", buf, sizeof(buf)) > 0)
        snprintf(oem_build_fingerprint, sizeof(oem_build_fingerprint), "%.*s", (int)sizeof(oem_build_fingerprint)-1, buf);

    /* ── 充电 ── */
    if (prop_get("ro.charger.protocol", buf, sizeof(buf)) > 0 ||
        prop_get("persist.vendor.charger.protocol", buf, sizeof(buf)) > 0) {
        snprintf(oem_charging_protocol, sizeof(oem_charging_protocol), "%.*s", (int)sizeof(oem_charging_protocol)-1, buf);
    }
    if (prop_get("ro.vendor.max.charging.power", buf, sizeof(buf)) > 0 ||
        prop_get("persist.vendor.charging.watt", buf, sizeof(buf)) > 0) {
        snprintf(oem_charging_watt, sizeof(oem_charging_watt), "%.*s", (int)sizeof(oem_charging_watt)-1, buf);
    }
    if (prop_get("ro.battery.capacity", buf, sizeof(buf)) > 0 ||
        prop_get("ro.vendor.battery.capacity", buf, sizeof(buf)) > 0) {
        snprintf(oem_battery_capacity, sizeof(oem_battery_capacity), "%.*s", (int)sizeof(oem_battery_capacity)-1, buf);
    }
    if (prop_get("persist.vendor.battery.dualcell", buf, sizeof(buf)) > 0)
        oem_dual_cell = atoi(buf) ? 1 : 0;

    /* ── 显示 ── */
    if (prop_get("ro.vendor.display.type", buf, sizeof(buf)) > 0 ||
        prop_get("ro.display.type", buf, sizeof(buf)) > 0) {
        snprintf(oem_display_panel, sizeof(oem_display_panel), "%.*s", (int)sizeof(oem_display_panel)-1, buf);
    }
    if (prop_get("ro.vendor.display.panel", buf, sizeof(buf)) > 0)
        snprintf(oem_display_vendor, sizeof(oem_display_vendor), "%.*s", (int)sizeof(oem_display_vendor)-1, buf);
    if (prop_get("ro.vendor.display.max.refresh.rate", buf, sizeof(buf)) > 0)
        snprintf(oem_display_max_hz, sizeof(oem_display_max_hz), "%.*s", (int)sizeof(oem_display_max_hz)-1, buf);
    if (prop_get("ro.vendor.display.peak.brightness", buf, sizeof(buf)) > 0)
        snprintf(oem_display_peak_nits, sizeof(oem_display_peak_nits), "%.*s", (int)sizeof(oem_display_peak_nits)-1, buf);

    /* ── 调度器（从 sysfs 读，更可靠）── */
    char g[64];
    if (read_str("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor", g, sizeof(g)) > 0)
        snprintf(oem_cpu_governor, sizeof(oem_cpu_governor), "%s", g);
    if (gpu_governor[0]) snprintf(oem_gpu_governor, sizeof(oem_gpu_governor), "%s", gpu_governor);

    /* ── 安全 ── */
    if (prop_get("ro.boot.verifiedbootstate", buf, sizeof(buf)) > 0) {
        snprintf(oem_verified_boot, sizeof(oem_verified_boot), "%.*s", (int)sizeof(oem_verified_boot)-1, buf);
        oem_secure_boot = (strcmp(buf, "green") == 0) ? 1 : 0;
    }
    if (prop_get("ro.hardware.security", buf, sizeof(buf)) > 0 ||
        prop_get("ro.boot.hardware.security", buf, sizeof(buf)) > 0) {
        snprintf(oem_security_chip, sizeof(oem_security_chip), "%.*s", (int)sizeof(oem_security_chip)-1, buf);
    }

    /* ── 模式开关 ── */
    if (prop_get("persist.sys.game_mode", buf, sizeof(buf)) > 0 ||
        prop_get("persist.sys.miui_game_mode", buf, sizeof(buf)) > 0)
        snprintf(oem_game_mode, sizeof(oem_game_mode), "%.*s", (int)sizeof(oem_game_mode)-1, buf);
    if (prop_get("persist.sys.performance_mode", buf, sizeof(buf)) > 0 ||
        prop_get("persist.sys.miui_perf_mode", buf, sizeof(buf)) > 0)
        snprintf(oem_perf_mode, sizeof(oem_perf_mode), "%.*s", (int)sizeof(oem_perf_mode)-1, buf);
    if (prop_get("persist.sys.battery_saver", buf, sizeof(buf)) > 0 ||
        prop_get("persist.sys.power_mode", buf, sizeof(buf)) > 0)
        snprintf(oem_power_save, sizeof(oem_power_save), "%.*s", (int)sizeof(oem_power_save)-1, buf);
    if (prop_get("persist.sys.ultra_battery_saver", buf, sizeof(buf)) > 0 ||
        prop_get("persist.sys.ultra_powersave", buf, sizeof(buf)) > 0)
        snprintf(oem_ultra_power_save, sizeof(oem_ultra_power_save), "%.*s", (int)sizeof(oem_ultra_power_save)-1, buf);

    /* ── 特性 ── */
    if (prop_get("persist.sys.hypermind.enable", buf, sizeof(buf)) > 0)
        snprintf(oem_ai_engine, sizeof(oem_ai_engine), "HyperMind %.*s", (int)sizeof(oem_ai_engine)-11, buf);
    if (prop_get("persist.sys.memory_extension.size", buf, sizeof(buf)) > 0)
        snprintf(oem_memory_fusion, sizeof(oem_memory_fusion), "%.*s", (int)sizeof(oem_memory_fusion)-1, buf);
    if (prop_get("persist.sys.ufs_turbo", buf, sizeof(buf)) > 0)
        snprintf(oem_storage_boost, sizeof(oem_storage_boost), "%.*s", (int)sizeof(oem_storage_boost)-1, buf);
    /* 散热方案：多家属性名不同，逐个试 */
    const char *therm[] = {"ro.vendor.thermal.solution", "persist.sys.thermal_solution", NULL};
    for (int i = 0; therm[i]; i++) {
        if (prop_get(therm[i], buf, sizeof(buf)) > 0) {
            snprintf(oem_thermal_solution, sizeof(oem_thermal_solution), "%.*s", (int)sizeof(oem_thermal_solution)-1, buf);
            break;
        }
    }
    (void)got_any;
}

/* 系统属性：SDK / Android 版本 / 补丁级别 / bootloader / SoC */
static void update_sys_props(void) {
    time_t tn = time(NULL);
    static time_t last = 0;
    if (tn - last < 300) return;   /* 静态信息，5 分钟即可 */
    last = tn;

    /* __system_property_get 需要 <sys/system_properties.h>，为免依赖，
       直接用 /system/build.prop + /vendor/build.prop + /proc/version 兜底 */
    const char *props[] = {
        "/system/build.prop",
        "/vendor/build.prop",
        "/product/build.prop",
        "/odm/etc/build.prop",
        "/system/odm/etc/build.prop",
        NULL
    };
    for (int i = 0; props[i]; i++) {
        FILE *f = fopen(props[i], "r");
        if (!f) continue;
        char l[512];
        while (fgets(l, sizeof(l), f)) {
            char *nl = strchr(l, '\n'); if (nl) *nl = 0;
            char *eq = strchr(l, '='); if (!eq) continue;
            *eq = 0; const char *k = l, *v = eq + 1;
            if (!strcmp(k, "ro.build.version.sdk")) sys_sdk_level = atoi(v);
            else if (!strcmp(k, "ro.build.version.release")) { strncpy(sys_android_ver, v, sizeof(sys_android_ver)-1); }
            else if (!strcmp(k, "ro.build.display.id")) { strncpy(sys_build_display, v, sizeof(sys_build_display)-1); }
            else if (!strcmp(k, "ro.build.version.security_patch")) { strncpy(sys_security_patch, v, sizeof(sys_security_patch)-1); }
            else if (!strcmp(k, "ro.bootloader")) { strncpy(sys_bootloader, v, sizeof(sys_bootloader)-1); }
            else if (!strcmp(k, "ro.product.board") || !strcmp(k, "ro.board.platform")) {
                if (sys_soc_model[0] == 0) strncpy(sys_soc_model, v, sizeof(sys_soc_model)-1);
            }
            else if (!strcmp(k, "ro.soc.model") || !strcmp(k, "ro.soc.manufacturer")) {
                if (!strcmp(k, "ro.soc.manufacturer")) strncpy(sys_soc_vendor, v, sizeof(sys_soc_vendor)-1);
                else strncpy(sys_soc_model, v, sizeof(sys_soc_model)-1);
            }
        }
        fclose(f);
    }
}

static void update_battery(void) {
    char path[MAX_LINE]; int v;
    snprintf(path, sizeof(path), "%s/capacity", SYSFS);
    if ((v = read_int(path)) >= 0) bat_capacity = v;
    snprintf(path, sizeof(path), "%s/temp", SYSFS);
    if ((v = read_int(path)) >= 0) bat_temp = (v > 200) ? v / 10 : v;  /* 自适应 0.1C 或直读 C */
    snprintf(path, sizeof(path), "%s/voltage_now", SYSFS);
    if ((v = read_int(path)) >= 0) bat_volt = v / 1000;
    snprintf(path, sizeof(path), "%s/current_now", SYSFS);
    if ((v = read_int(path)) >= 0) bat_curr = v / 1000;
    snprintf(path, sizeof(path), "%s/status", SYSFS);
    read_str(path, bat_status, sizeof(bat_status));
    snprintf(path, sizeof(path), "%s/cycle_count", SYSFS);
    if ((v = read_int(path)) >= 0) cycle_count = v;
    snprintf(path, sizeof(path), "%s/charge_full", SYSFS);
    if ((v = read_int(path)) >= 0) charge_full_mah = v / 1000;
    /* ★ 动态读设计容量（替代硬编码 5380） */
    snprintf(path, sizeof(path), "%s/charge_full_design", SYSFS);
    if ((v = read_int(path)) > 0) design_mah = v / 1000;
    /* ★ 读电芯原生 SOH（比自算准） */
    snprintf(path, sizeof(path), "/sys/class/power_supply/bms/soh");
    v = read_int(path);
    if (v < 0) { snprintf(path, sizeof(path), "%s/soh", SYSFS); v = read_int(path); }
    soh_native = v;
    /* bms/capacity_raw 高精度 */
    snprintf(path, sizeof(path), "%s/bms/capacity_raw", SYSFS);
    if ((v = read_int(path)) < 0) { snprintf(path, sizeof(path), "%s/capacity_raw", SYSFS); v = read_int(path); }
    if (v >= 0) capacity_disp = v; else capacity_disp = bat_capacity * 100;
    /* USB */
    usb_online = read_int(SYSFS_USB "/online");
    bat_input_current_limit = read_int(SYSFS_USB "/input_current_limit");
    usb_curr_cache = read_int(SYSFS_USB "/current_now");
    usb_volt_cache = read_int(SYSFS_USB "/voltage_now");
    read_str(SYSFS_USB "/real_type", proto_name, sizeof(proto_name));
    if (proto_name[0] == 0) read_str(SYSFS_USB "/type", proto_name, sizeof(proto_name));
    pd_type = read_int(SYSFS_USB "/pd_type");
    usb_power_max = read_int(SYSFS_USB "/power_max");
    if (usb_power_max < 0) usb_power_max = read_int(SYSFS_USB "/max_power");
    /* 功率 */
    int vol_mv = bat_volt > 0 ? bat_volt : 0;
    int cur_ma = bat_curr;
    if (vol_mv > 0 && cur_ma != 0) power_mw = vol_mv * cur_ma / 1000;
    if (vol_mv > 0 && cur_ma < 0) {
        int remain = bat_capacity >= 0 ? (charge_limit > 0 ? charge_limit - bat_capacity : 100 - bat_capacity) : 0;
        if (remain > 0 && cur_ma != 0) {
            int cmA = -cur_ma; /* 充电电流为正 */
            if (cmA > 0) est_full_min = (int)((long long)remain * charge_full_mah * 60 / cmA / 100);
        }
    }
    /* 健康度：优先电芯原生 bms/soh，回退到自算 */
    if (soh_native > 0 && soh_native <= 100) {
        health = soh_native;
    } else if (cycle_count > 0 && charge_full_mah > 0 && design_mah > 0) {
        float health_f = (float)charge_full_mah / (float)design_mah * 100.0f;
        if (health_f > 100) health_f = 100;
        if (health_f < 0) health_f = 0;
        health = (int)(health_f * 10 + 0.5f) / 10.0f;
    }
    /* 剩余循环估算（用真实设计容量，不再硬编码） */
    if (cycle_count > 0 && charge_full_mah > 0 && design_mah > 0) {
        float loss = (float)(design_mah - charge_full_mah) / cycle_count;
        if (loss > 0.001f) {
            int est = (int)((charge_full_mah * 0.6f - design_mah * 0.6f) / loss);
            if (est < 0) est = 0;
            est_cycles_left = est;
        }
    }
    if (health > 0) {
        if (health >= 95) strcpy(health_rating, "优秀");
        else if (health >= 85) strcpy(health_rating, "良好");
        else if (health >= 70) strcpy(health_rating, "一般");
        else if (health >= 60) strcpy(health_rating, "较差");
        else strcpy(health_rating, "需更换");
    }
    slow_update_battery();
}

/* ★ v1.3.1 P2-11: 真旁路控制（MTK en_power_path）
 * en_power_path=1 → 电源路径开启（正常充电/供电）
 * en_power_path=0 → 关闭电源路径 = 系统由充电器直供，电池不充不放（真旁路）
 * ⚠️ 安全护栏：电量低于 bypass_min_soc 时禁止启用（防断电）
 *              拔充电器时自动恢复 1（防无电关机）
 */
/* ★ v1.3.1 P2-12/13: 恒压点调整 + 充电电流限流（MTK）
 * set_cv: 4430mV(默认) → 4200mV 可延长寿命（充不满但电池更耐久）
 * current_cmd: "<enable> <discharge>" 格式，参考 MTK battery_common.c
 * ⚠️ 都会备份原值到 x_orig_cv，关闭功能时还原
 */
/* ★ v1.3.1 P2-14: 复用 MTK 原生夜间充电（固件层，比自研轮询更省电） */
static void apply_native_night(void) {
    static int last = -1;
    const char *NP = SYSFS "/night_charging";
    if (access(NP, W_OK) != 0) return;   /* 节点不支持：不清用户设置 */
    int want = x_use_native_night ? 1 : 0;
    if (want != last) {
        char v[4]; snprintf(v, sizeof(v), "%d", want);
        if (write_str(NP, v) == 0) {
            last = want;
            log_event(want ? "已启用系统原生夜间充电" : "已关闭系统原生夜间充电");
        } else x_use_native_night = 0;
    }
}

/* ★ v1.4.0: 恒压点/限流的"已应用"状态改为文件级持久标志。
   原先用函数内 static，进程一重启就归 0，
   于是「关掉恒压点」的还原分支永远进不去 —— 电池被永久锁在 4200mV。 */
static int cv_applied = 0;
static int cc_applied = 0;

/* ★ v1.4.0: 启动时抓一次"原厂恒压点"。
   必须在任何写入发生之前调用（main 里紧跟 update_battery），
   否则读到的是上一次自己写进去的值，备份就废了。 */
static void snapshot_orig_cv(void) {
    const char *CV = "/proc/mtk_battery_cmd/set_cv";
    if (access(CV, R_OK) != 0) return;
    if (x_orig_cv > 0) return;              /* conf 里已有可信备份，别覆盖 */
    int v = read_int(CV);
    /* 合理范围才认；读失败(-1)或越界一律不写，避免存进垃圾值 */
    if (v >= 3900 && v <= 4500) {
        x_orig_cv = v;
        log_event("已记录原厂恒压点");
    }
}

static void apply_cv_and_current(void) {
    const char *CV = "/proc/mtk_battery_cmd/set_cv";
    const char *CC = "/proc/mtk_battery_cmd/current_cmd";
    static int cc_last_ma = -1;

    /* ★ v1.4.0: cv_applied/cc_applied 是内存态，重启即归 0，
       导致"关掉恒压点"的还原分支永远进不去。
       这里在首次进入时按 conf 的开关状态重建。 */
    static int inited = 0;
    if (!inited) {
        inited = 1;
        if (x_set_cv_en) cv_applied = 1;
        if (x_limit_current_en) cc_applied = 1;
    }

    /* ── 恒压点 ── */
    if (access(CV, W_OK) == 0) {
        if (x_set_cv_en && x_set_cv_mv >= 3900 && x_set_cv_mv <= 4500) {
            if (x_orig_cv < 0) snapshot_orig_cv();   /* 兜底：仍未有备份就现抓 */
            char v[16]; snprintf(v, sizeof(v), "%d", x_set_cv_mv);
            if (write_str(CV, v) == 0) cv_applied = 1;
        } else if (cv_applied && x_orig_cv > 0) {
            char v[16]; snprintf(v, sizeof(v), "%d", x_orig_cv);
            if (write_str(CV, v) == 0) {
                cv_applied = 0;
                log_event("恒压点已还原");
            }
        }
    }

    /* ── 充电电流限流 ──
       真正的限流节点是 input_current_limit(µA)，与 current_cmd 无关。
       ★ v1.4.0 修复三点：
         1) 原实现把整个限流块嵌在 if (access(current_cmd, W_OK)) 里，
            没有 current_cmd 的机型（有 input_current_limit 但无该 proc 节点）
            会静默失效 —— 现在改为直接检查 input_current_limit 是否可写。
         2) 原实现第一段硬编码 "0 0"，跟 x_limit_current_ma 完全无关（空转）。
         3) 关闭时不复位，节点被永久留在限流值上。
       current_cmd 若存在仍顺手写一次（部分 MTK 需要它解锁），但不再作为前置条件。 */
    {
        static const char *IL_CANDIDATES[] = {
            "/sys/class/power_supply/mtk-master-charger/input_current_limit",
            "/sys/class/power_supply/battery/input_current_limit",
            "/sys/class/power_supply/usb/input_current_limit",
            NULL
        };
        const char *IL = NULL;
        for (int i = 0; IL_CANDIDATES[i]; i++) {
            if (access(IL_CANDIDATES[i], W_OK) == 0) { IL = IL_CANDIDATES[i]; break; }
        }
        /* 顺带：若 current_cmd 存在，写一次解锁标志（不影响限流主逻辑） */
        if (access(CC, W_OK) == 0)
            write_str(CC, "0 0");

        if (IL) {
            if (x_limit_current_en && x_limit_current_ma > 0 && x_limit_current_ma <= 6000) {
                if (cc_last_ma != x_limit_current_ma) {
                    char ma[16]; snprintf(ma, sizeof(ma), "%d", x_limit_current_ma * 1000);
                    if (write_str(IL, ma) == 0) {
                        cc_last_ma = x_limit_current_ma;
                        cc_applied = 1;
                        log_event("充电电流已限流");
                    }
                }
            } else if (cc_applied) {
                /* 关闭：写回节点自身上限，交还内核调度 */
                int cap = read_int(IL);
                int restore = (cap > x_limit_current_ma * 1000) ? cap : 6000000;
                char ma[16]; snprintf(ma, sizeof(ma), "%d", restore);
                write_str(IL, ma);
                cc_last_ma = -1;
                cc_applied = 0;
                log_event("电流限制已解除");
            }
        } else {
            /* 本机无任何可写的限流节点：不清用户设置，仅不执行（与原策略一致） */
            if (x_limit_current_en) log_event("本机无电流限流节点");
        }
    }
}

static void apply_power_path(void) {
    static int last_pp = -1;
    const char *PP = "/proc/mtk_battery_cmd/en_power_path";
    if (access(PP, W_OK) != 0) return;   /* 节点不支持：不清用户设置，仅不执行 */

    int want = 1;
    if (x_en_power_path) {
        /* 想旁路：必须插着电 且 电量 >= 护栏 */
        if (usb_online > 0 && bat_capacity >= x_bypass_min_soc && bat_capacity < 98)
            want = 0;
        else
            want = 1;   /* 条件不满足 → 恢复供电 */
    }
    if (want != last_pp) {
        char v[8]; snprintf(v, sizeof(v), "%d", want);
        if (write_str(PP, v) == 0) {
            last_pp = want;
            log_event(want ? "电源路径恢复(充电/供电)" : "真旁路已启用(系统直供)");
        } else {
            char msg[96]; snprintf(msg, sizeof(msg), "写 en_power_path 失败");
            log_event(msg);
            x_en_power_path = 0;   /* 失败则放弃 */
        }
    }
}

static void apply_control(void) {
    char path[MAX_LINE];
    snprintf(path, sizeof(path), "%s/input_suspend", SYSFS);

    /* 温度保护永远优先（手动充满期间也生效）*/
    if (bat_temp >= temp_limit) {
        if (write_str(path, "1") < 0) log_event("写input_suspend失败(温度保护)");
        temp_suspended = 1;
        return;
    }
    if (temp_suspended) {
        if (!paused) {
            if (write_str(path, "0") < 0) log_event("写input_suspend失败(恢复)");
        }
        temp_suspended = 0;
    }
    /* ★ 新增：充电IC 过温保护（电池温度正常但充电IC 过热时也停充，防烧） */
    if (chg_temp > 0 && chg_temp >= chg_temp_limit) {
        if (write_str(path, "1") < 0) log_event("写input_suspend失败(充电IC过温)");
        if (!chg_temp_suspended) log_event("充电IC过温保护触发");
        chg_temp_suspended = 1;
        return;
    }
    if (chg_temp_suspended) {
        if (!paused && chg_temp > 0 && chg_temp < chg_temp_limit - 5) {
            if (write_str(path, "0") < 0) log_event("写input_suspend失败(IC恢复)");
            chg_temp_suspended = 0;
        } else {
            return;   /* 未降到恢复线以下，保持停充 */
        }
    }
    if (paused) {
        if (write_str(path, "1") < 0) log_event("写input_suspend失败(暂停)");
        return;
    }

    /* 手动充满：仅跳过容量上限 */
    if (full_once) {
        if (time(NULL) >= full_until) {
            full_once = 0;
            log_event("手动充满结束，恢复充电限制");
        } else {
            if (write_str(path, "0") < 0) log_event("写input_suspend失败(充满)");
            return;
        }
    }

    /* 软旁路：维持电量在 [55,60]，充电器直供系统 */
    if (bypass_soft) {
        int ns2 = -1;
        if (bat_capacity >= BYPASS_HI) ns2 = 1;
        else if (bat_capacity <= BYPASS_LO) ns2 = 0;
        if (ns2 >= 0 && ns2 != last_suspend_val) {
            if (write_str(path, ns2 ? "1" : "0") < 0) log_event("写input_suspend失败(软旁路)");
            last_suspend_val = ns2;
        }
        return;
    }
    if (!limit_enabled) {
        if (write_str(path, "0") < 0) log_event("写input_suspend失败(关闭)");
        temp_suspended = 0; return;
    }
    int new_suspend = -1;
    if (bat_capacity >= charge_limit) new_suspend = 1;
    else if (bat_capacity <= charge_limit - resume_delta) new_suspend = 0;
    if (new_suspend >= 0 && new_suspend != last_suspend_val) {
        if (write_str(path, new_suspend ? "1" : "0") < 0) log_event("写input_suspend失败");
        last_suspend_val = new_suspend;
    }
}
static void load_config(void) {
    FILE *f = fopen(CONFIG, "r"); if (!f) return;
    char line[MAX_LINE];
    while (fgets(line, sizeof(line), f)) {
        if (line[0]=='#'||line[0]=='\n') continue;
        char *p = strchr(line, '\n'); if (p) *p = '\0';
        char *eq = strchr(line, '='); if (!eq) continue;
        *eq = '\0';
        const char *k = line, *v = eq + 1;
        /* ★ v1.3.1: 范围校验（防配置被改坏：charge_limit=0 会导致永不充电） */
        int x;
        if (!strcmp(k, "charge_limit")) { x = atoi(v); charge_limit = (x >= 20 && x <= 100) ? x : 80; }
        else if (!strcmp(k, "temp_limit")) { x = atoi(v); temp_limit = (x >= 30 && x <= 60) ? x : 45; }
        else if (!strcmp(k, "chg_temp_limit")) { x = atoi(v); chg_temp_limit = (x >= 40 && x <= 90) ? x : 60; }
        else if (!strcmp(k, "resume_delta")) { x = atoi(v); resume_delta = (x >= 1 && x <= 20) ? x : 5; }
        else if (!strcmp(k, "interval")) { x = atoi(v); interval = (x >= 1 && x <= 60) ? x : 5; }
        else if (!strcmp(k, "enabled")) limit_enabled = atoi(v) ? 1 : 0;
        else if (!strcmp(k, "history_enabled")) history_enabled = atoi(v) ? 1 : 0;
        /* ★ v1.4.0: 高级控制开关纳入主配置。
           原先只存 extras.json，一旦该文件损坏/丢失，开关会静默丢失；
           x_orig_cv 更是只存内存 ⇒ 重启后关不掉恒压点会永久停在 4200mV。 */
        else if (!strcmp(k, "xpp")) x_en_power_path = atoi(v) ? 1 : 0;
        else if (!strcmp(k, "xcv")) x_set_cv_en = atoi(v) ? 1 : 0;
        else if (!strcmp(k, "xcvmv")) { x = atoi(v); x_set_cv_mv = (x == 0 || (x >= 3900 && x <= 4500)) ? x : 0; }
        else if (!strcmp(k, "xcc")) x_limit_current_en = atoi(v) ? 1 : 0;
        else if (!strcmp(k, "xccma")) { x = atoi(v); x_limit_current_ma = (x >= 0 && x <= 6000) ? x : 0; }
        else if (!strcmp(k, "xnight")) x_use_native_night = atoi(v) ? 1 : 0;
        else if (!strcmp(k, "xminsoc")) { x = atoi(v); x_bypass_min_soc = (x >= 20 && x <= 80) ? x : 50; }
        else if (!strcmp(k, "xorigcv")) { x = atoi(v); x_orig_cv = (x > 0) ? x : -1; }
    }
    fclose(f);
}
static void save_config(void) {
    FILE *f = fopen(CONFIG, "w"); if (!f) return;
    fprintf(f, "charge_limit=%d\n", charge_limit);
    fprintf(f, "temp_limit=%d\n", temp_limit);
    fprintf(f, "chg_temp_limit=%d\n", chg_temp_limit);
    fprintf(f, "resume_delta=%d\n", resume_delta);
    fprintf(f, "interval=%d\n", interval);
    fprintf(f, "enabled=%d\n", limit_enabled);
    fprintf(f, "history_enabled=%d\n", history_enabled);
    /* ★ v1.4.0: 高级控制开关 + 恒压点原值备份（重启可还原） */
    fprintf(f, "xpp=%d\n", x_en_power_path);
    fprintf(f, "xcv=%d\n", x_set_cv_en);
    fprintf(f, "xcvmv=%d\n", x_set_cv_mv);
    fprintf(f, "xcc=%d\n", x_limit_current_en);
    fprintf(f, "xccma=%d\n", x_limit_current_ma);
    fprintf(f, "xnight=%d\n", x_use_native_night);
    fprintf(f, "xminsoc=%d\n", x_bypass_min_soc);
    fprintf(f, "xorigcv=%d\n", x_orig_cv);
    fclose(f);
}

static void save_stats(void) {
    FILE *f = fopen("/data/adb/battery-manager/stats.json", "w");
    if (!f) return;
    fprintf(f, "{\"count\":%d,\"min\":%ld,\"mah\":%ld}", stats_count, stats_min, stats_mah);
    fclose(f);
}
static void load_extras(void) {
    FILE *f = fopen("/data/adb/battery-manager/extras.json", "r");
    if (!f) return;
    char buf[512]; size_t n = fread(buf, 1, sizeof(buf)-1, f); fclose(f);
    buf[n] = 0;
    char *p = strstr(buf, "\"webhook\":\"");
    if (p) {
        p += 11; int i = 0;
        while (*p && *p != '"' && i < 255) webhook_url[i++] = *p++;
        webhook_url[i] = 0;
    }
    /* 逐字段解析（save_extras 中 webhook 位于 scene 与 lang 之间，整串 sscanf 会失配）*/
    char *q;
    if ((q = strstr(buf, "\"night\":"))) night_enabled = atoi(q + 8);
    if ((q = strstr(buf, "\"ns\":"))) night_start_h = atoi(q + 5);
    if ((q = strstr(buf, "\"ne\":"))) night_end_h = atoi(q + 5);
    if ((q = strstr(buf, "\"scene\":"))) scene = atoi(q + 8);
    if ((q = strstr(buf, "\"lang\":"))) lang = atoi(q + 7);
    if ((q = strstr(buf, "\"theme\":"))) theme = atoi(q + 8);
    if ((q = strstr(buf, "\"bypass_soft\":"))) bypass_soft = atoi(q + 14);
    /* ★ v1.3.1 P2 新配置（注意每个字段名的精确长度）
       ★ v1.4.0: 改为「extras.json 优先、batt.conf 兜底」。
       load_config() 先跑并从 batt.conf 读这些键，
       但 extras.json 才是它们的主存处（save_extras 每次写入）。
       这里只在 extras.json 真的存在该字段时才覆盖，
       避免旧版本残留的不完整 extras.json 把 batt.conf 的值清成 0。 */
    if ((q = strstr(buf, "\"xpp\":"))) x_en_power_path = atoi(q + 6);
    if ((q = strstr(buf, "\"xcv\":"))) x_set_cv_en = atoi(q + 6);
    if ((q = strstr(buf, "\"xcvmv\":"))) x_set_cv_mv = atoi(q + 8);
    if ((q = strstr(buf, "\"xlc\":"))) x_limit_current_en = atoi(q + 6);
    if ((q = strstr(buf, "\"xlcma\":"))) x_limit_current_ma = atoi(q + 8);
    if ((q = strstr(buf, "\"xnn\":"))) x_use_native_night = atoi(q + 6);
    if ((q = strstr(buf, "\"xbminsoc\":"))) x_bypass_min_soc = atoi(q + 11);
    /* ★ v1.4.0: 恒压点原值备份 —— extras.json 里没有就沿用 batt.conf 的值。
       （x_orig_cv 只在 extras 里存过一次的话，老版本升级上来的用户能保住备份） */
    if ((q = strstr(buf, "\"xorigcv\":"))) { int t = atoi(q + 10); if (t > 0) x_orig_cv = t; }
}
static void save_extras(void) {
    FILE *f = fopen("/data/adb/battery-manager/extras.json", "w");
    if (!f) return;
    /* ★ v1.4.0: 追加 xorigcv（恒压点原值备份）。
       不存它的话，重启后 x_orig_cv=-1，用户关掉恒压点调整会还原不回去。 */
    fprintf(f, "{\"night\":%d,\"ns\":%d,\"ne\":%d,\"scene\":%d,\"webhook\":\"%s\",\"lang\":%d,\"theme\":%d,\"bypass_soft\":%d,"
        "\"xpp\":%d,\"xcv\":%d,\"xcvmv\":%d,\"xlc\":%d,\"xlcma\":%d,\"xnn\":%d,\"xbminsoc\":%d,\"xorigcv\":%d}",
        night_enabled, night_start_h, night_end_h, scene, webhook_url, lang, theme, bypass_soft,
        x_en_power_path, x_set_cv_en, x_set_cv_mv, x_limit_current_en, x_limit_current_ma,
        x_use_native_night, x_bypass_min_soc, x_orig_cv);
    fclose(f);
}

static void load_stats(void) {
    FILE *f = fopen("/data/adb/battery-manager/stats.json", "r");
    if (!f) return;
    if (fscanf(f, "{\"count\":%d,\"min\":%ld,\"mah\":%ld}", &stats_count, &stats_min, &stats_mah) < 3)
        { stats_count = 0; stats_min = 0; stats_mah = 0; }
    fclose(f);
}
static void handle_charging_state(void) {
    int charging = strcmp(bat_status, "Charging") == 0;
    if (charging && !sess_active) {
        sess_start = time(NULL); sess_start_cap = bat_capacity;
        sess_active = 1; sess_min = 0; sess_mah = 0;
        cc_peak = 0;   /* v1.5.0: 新一轮充电，重置 CC/CV 判据基准 */
        cal_log("开始新的充电会话");
        /* v1.3.1: 记录硬件累计电荷起点(uAh) */
        sess_start_counter = read_int(SYSFS "/charge_counter");
        if (sess_start_counter < 0) sess_start_counter = -1;
    } else if (charging) {
        sess_min = (int)((time(NULL) - sess_start) / 60);
    }
    if (!charging && sess_active) {
        sess_active = 0;
        if (sess_min > 0 || sess_mah > 0) {
            stats_count++; stats_min += sess_min; stats_mah += sess_mah;
            save_stats();
        }
        sess_start_counter = -1;
    }
    /* v1.3.1: 优先用硬件 charge_counter 差值(精确)，回退到容量估算 */
    if (sess_active) {
        int cnow = read_int(SYSFS "/charge_counter");
        if (sess_start_counter >= 0 && cnow >= 0 && cnow >= sess_start_counter) {
            sess_mah = (int)((cnow - sess_start_counter) / 1000);
        } else if (sess_start_cap >= 0 && bat_capacity > sess_start_cap) {
            int cf = read_int(SYSFS "/charge_full");
            int chg_full = cf > 0 ? cf / 1000 : (design_mah > 0 ? design_mah : 4000);
            sess_mah = ((bat_capacity - sess_start_cap) * chg_full) / 100;
        }
        if (sess_mah < 0) sess_mah = 0;
    }
}

static int in_night_window(void) {
    if (!night_enabled) return 0;
    if (night_start_h == night_end_h) return 0;
    time_t t = time(NULL); struct tm *tm = localtime(&t);
    int h = tm->tm_hour;
    if (night_start_h < night_end_h) return h >= night_start_h && h < night_end_h;
    if (h >= night_start_h) return 1;
    return h < night_end_h;
}

static void apply_night_mode(void) { static time_t nl = 0; time_t tn = time(NULL); if (tn - nl < 30) return; nl = tn;
    if (!night_enabled) return;
    /* 手动暂停时，夜间模式让位用户 */
    if (paused) return;
    if (in_night_window() && bat_capacity >= charge_limit) {
        if (write_str(SYSFS "/input_suspend", "1") < 0) log_event("night write fail");
    } else if (night_enabled && !in_night_window() && bat_capacity < charge_limit && bat_curr <= 0) {
        if (write_str(SYSFS "/input_suspend", "0") < 0) log_event("night resume fail");
    }
}

static void high_temp_alert(void) { static time_t hl = 0; time_t tn = time(NULL); if (tn - hl < 10) return; hl = tn;
    if (bat_temp >= 50 && !alerted_high) {
        alerted_high = 1;
        log_event("⚠️ high temp alert");
        if (webhook_url[0]) {
            char cmd[512];
            /* 安全：URL 必须是 http:// 或 https:// 开头，且不含 shell 特殊字符 */
            if ((strncmp(webhook_url, "http://", 7) == 0 || strncmp(webhook_url, "https://", 8) == 0) &&
                !strchr(webhook_url, '\'') && !strchr(webhook_url, '"') &&
                !strchr(webhook_url, '$') && !strchr(webhook_url, ';') &&
                !strchr(webhook_url, '`') && !strchr(webhook_url, '\\') &&
                !strchr(webhook_url, '|') && !strchr(webhook_url, '&') &&
                !strchr(webhook_url, '<')) {
                /* ★ v1.3.1: 用 busybox wget 代替 curl（Android 无 curl） */
                const char *bb = NULL;
                if (access("/data/adb/ap/bin/busybox", X_OK) == 0) bb = "/data/adb/ap/bin/busybox";
                else if (access("/data/adb/ksu/bin/busybox", X_OK) == 0) bb = "/data/adb/ksu/bin/busybox";
                else if (access("/data/adb/magisk/busybox", X_OK) == 0) bb = "/data/adb/magisk/busybox";
                if (bb) {
                    snprintf(cmd, sizeof(cmd), "%s wget -q -T 5 -O /dev/null --post-data='temp=%d' '%s' >/dev/null 2>&1",
                             bb, bat_temp, webhook_url);
                    FILE *wh = popen(cmd, "r");
                    if (wh) pclose(wh);
                    log_event("webhook sent (busybox)");
                } else {
                    log_event("webhook skipped: no busybox");
                }
            } else {
                log_event("webhook URL invalid chars");
            }
        }
    }
    if (bat_temp < 45) alerted_high = 0;
}

/* ★ v1.5.0: 充电阶段识别
   判据（按 MTK 锂电充电流程）：
     - 未充电/放电           → 0
     - 充电中且电量 <10%、电流小 → 1 预充(trickle)
     - 充电中且电流基本稳定、电压上升 → 2 恒流 CC
     - 充电中且电流明显下降、电压接近恒压点 → 3 恒压 CV
     - status=Full 或充电完成 → 4 充满
   电流单位 mA（bat_curr 已是 mA），电压 mV。 */
static int detect_charge_stage(void) {
    int charging = strcmp(bat_status, "Charging") == 0;
    int full = strcmp(bat_status, "Full") == 0;
    if (full) return 4;
    if (!charging) return 0;
    if (bat_capacity >= 0 && bat_capacity < 10 && bat_curr < 300) return 1;

    /* CV 判据 + 迟滞（v1.5.0 修正抖动）
       首次实测：仅比较「本轮电流 < 上轮峰值 60%」会在电流小幅波动时
       于 CC<->CV 反复横跳（实测 39s CC / 44s CV 来回切）。
       现在：
         · cc_peak 记录本轮充电的电流峰值（充电开始时重置）
         · 进入 CV 需 电流 < 峰值 60%（较严）
         · 退出 CV 需 电流 > 峰值 75%（较松）—— 两阈值拉开形成迟滞带
         · 且必须电压确实接近恒压点，避免低温大电流误判 */
    int cv_mv = x_orig_cv > 0 ? x_orig_cv : 4430;
    if (bat_curr > cc_peak) cc_peak = bat_curr;
    if (cc_peak <= 0) cc_peak = bat_curr;

    int near_cv = (bat_volt >= cv_mv - 80);
    if (chg_stage == 3) {
        if (bat_curr > cc_peak * 75 / 100 || !near_cv) return 2;
        return 3;
    }
    if (near_cv && bat_curr > 0 && bat_curr < cc_peak * 6 / 10) return 3;
    return 2;
}

/* ★ v1.5.0: 电池校准状态机
   LiCo/LiPo 电量计(库仑计)长期部分充放会累积漂移，表现为"显示 80% 但实际已到 20%"
   或"到 100% 后还能充很久"。校准 = 完整走一次 满→空→满，让 gauge 重新对齐。

   流程：
     idle ──start──> charging_full ──(到100%)──> resting ──(静置2h)──> discharging
              ──(到10%)──> charging_up ──(充满)──> done(重置计数)

   安全护栏（重要）：
     · 放电阶段【不主动耗电】，只提示用户拔充电器自然使用 —— 主动放电对电池有损
     · 电量 <5% 立即中止（防过放损伤）
     · 温度 >45°C 中止（防高温充电）
     · 全程写 calibrate.jsonl，可事后回溯 */
static const char *cal_state_name(int st) {
    switch (st) {
        case 1: return "charging_full";
        case 2: return "resting";
        case 3: return "discharging";
        case 4: return "charging_up";
        case 5: return "done";
        default: return "idle";
    }
}

static void save_cal_state(void) {
    FILE *f = fopen("/data/adb/battery-manager/calibrate.state", "w");
    if (!f) return;
    fprintf(f, "%d\n%ld\n%d\n%d\n%d\n", cal_state, (long)cal_started,
            cal_max_cap, cal_min_cap, cal_fulfilled);
    fclose(f);
}

static void load_cal_state(void) {
    FILE *f = fopen("/data/adb/battery-manager/calibrate.state", "r");
    if (!f) return;
    long t = 0;
    if (fscanf(f, "%d %ld %d %d %d", &cal_state, &t, &cal_max_cap, &cal_min_cap, &cal_fulfilled) >= 1)
        cal_started = (time_t)t;
    fclose(f);
}

static void cal_log(const char *msg) {
    FILE *f = fopen(CALIB_FILE, "a");
    if (!f) return;
    fprintf(f, "{\"t\":%ld,\"s\":%d,\"stage\":\"%s\",\"c\":%d,\"msg\":\"%s\"}\n",
            (long)time(NULL), cal_state, cal_state_name(cal_state), bat_capacity, msg);
    fclose(f);
}

static void cal_start(void) {
    cal_state = 1;
    cal_started = time(NULL);
    cal_max_cap = bat_capacity;
    cal_min_cap = bat_capacity >= 0 ? bat_capacity : 101;
    cal_fulfilled = 0;
    save_cal_state();
    cal_log("校准开始：请保持充电至 100%");
    log_event("校准模式已启动");
}

static void cal_abort(const char *why) {
    char m[128];
    snprintf(m, sizeof(m), "校准中止：%s", why);
    cal_state = 0;
    save_cal_state();
    cal_log(m);
    log_event(m);
}

/* 每轮 update_battery 后调用 */
static void cal_tick(void) {
    if (cal_state == 0 || cal_state == 5) return;

    int charging = strcmp(bat_status, "Charging") == 0;
    int full     = strcmp(bat_status, "Full") == 0;

    /* 通用护栏 */
    if (bat_capacity >= 0 && bat_capacity < 5 && cal_state != 1) {
        cal_abort("电量低于 5%（防过放）");
        return;
    }
    if (bat_temp > 450) { cal_abort("温度超过 45°C"); return; }

    if (bat_capacity > cal_max_cap) cal_max_cap = bat_capacity;
    if (bat_capacity >= 0 && bat_capacity < cal_min_cap) cal_min_cap = bat_capacity;

    switch (cal_state) {
        case 1: /* 等充满 */
            if (full || bat_capacity >= 100) {
                cal_fulfilled = 1;
                cal_state = 2;
                cal_started = time(NULL);   /* 静置计时起点 */
                save_cal_state();
                cal_log("已充满，请拔掉充电器静置 2 小时");
            }
            break;

        case 2: /* 静置：2 小时（不拔线也行，只要不充不放） */
            if (time(NULL) - cal_started >= 2 * 3600) {
                cal_state = 3;
                cal_started = time(NULL);
                save_cal_state();
                cal_log("静置完成，请断开充电器使用设备放电至 10%");
            }
            break;

        case 3: /* 放电：等用户用到 10% */
            if (charging && bat_capacity <= 15) {
                /* 用户提前插上了：直接进入回充阶段 */
                cal_state = 4;
                save_cal_state();
                cal_log("检测到回充，进入满充阶段");
            } else if (bat_capacity >= 0 && bat_capacity <= 10) {
                cal_state = 4;
                save_cal_state();
                cal_log("已放到 10%，请插上充电器充满以完成校准");
            }
            break;

        case 4: /* 回充到满，完成 */
            if (full || bat_capacity >= 100) {
                cal_state = 5;
                save_cal_state();
                cal_log("校准完成：电量计已重新对齐");
                log_event("电池校准已完成");
            }
            break;
    }
}

static char *cal_status_json(void) {
    char *out = malloc(512);
    if (!out) return strdup("{}");
    long elapsed = cal_started > 0 ? (long)(time(NULL) - cal_started) : 0;
    snprintf(out, 512,
        "{\"state\":%d,\"state_name\":\"%s\",\"elapsed_sec\":%ld,"
        "\"cap_now\":%d,\"cap_max\":%d,\"cap_min\":%d,\"fulfilled\":%d,"
        "\"temp\":%d,\"charging\":%d}",
        cal_state, cal_state_name(cal_state), elapsed,
        bat_capacity, cal_max_cap, cal_min_cap, cal_fulfilled,
        bat_temp, strcmp(bat_status, "Charging") == 0);
    return out;
}

static void push_history(void) {
    { time_t tn = time(NULL); static time_t hist_last = 0; if (tn - hist_last < 60) return; hist_last = tn; }
    time_t now = time(NULL);
    if (!history_enabled) return;
    if (now - last_save < 60) return;
    struct stat st;
    if (stat(HISTORY, &st) == 0 && st.st_size > 512 * 1024) {
        FILE *f = fopen(HISTORY, "r");
        if (f) {
            fseek(f, -256 * 1024, SEEK_END);
            long pos = ftell(f);
            if (pos < 0) { fclose(f); f = NULL; }
            else {
                long tail_len = st.st_size - pos;
                char *tail = malloc(tail_len + 1);
                if (!tail) { fclose(f); f = NULL; }
                else {
                    size_t _fr = fread(tail, 1, tail_len, f); (void)_fr;
                    fclose(f);
                    tail[tail_len] = '\0';
                    char *p = tail; while (*p && *p != '\n') p++;
                    if (*p == '\n') p++;
                    f = fopen(HISTORY, "w");
                    if (f) { fwrite(p, 1, strlen(p), f); fclose(f); }
                    free(tail);
                }
            }
        }
    }
    FILE *f = fopen(HISTORY, "a"); if (!f) return;
    fprintf(f, "{\"t\":%ld,\"c\":%d,\"tmp\":%d,\"v\":%d,\"s\":\"%s\",\"i\":%d,\"st\":%d}\n",
            (long)now, bat_capacity, bat_temp, bat_volt, bat_status, bat_curr, chg_stage);
    fclose(f);
    last_save = now;
    if (sess_active && now - session_30s >= 30) {
        session_30s = now;
        /* ★ v1.5.0: 增加 电流/电压/阶段，供充电曲线图使用
           i=电流mA  v=电压mV  st=阶段(0未充/1预充/2恒流/3恒压/4充满)
           ★ 同时做滚动截断：session.jsonl 是纯追加文件，长期会无限增长
             （每 30s 一条，一年 ≈ 100 万行 / 90MB）。
             超过 512KB 时保留尾部 256KB（约 2800 条 ≈ 23 小时采样）。 */
        FILE *sf = fopen(SESSION_FILE, "a");
        if (sf) {
            fprintf(sf, "{\"t\":%ld,\"c\":%d,\"mah\":%d,\"tmp\":%d,\"i\":%d,\"v\":%d,\"st\":%d}\n",
                (long)now, bat_capacity, sess_mah, bat_temp,
                bat_curr, bat_volt, chg_stage);
            fclose(sf);

            struct stat sst;
            if (stat(SESSION_FILE, &sst) == 0 && sst.st_size > 512 * 1024) {
                FILE *rf = fopen(SESSION_FILE, "r");
                if (rf) {
                    fseek(rf, -256 * 1024, SEEK_END);
                    long tail_len = sst.st_size - ftell(rf);
                    char *tail = malloc(tail_len + 1);
                    if (tail) {
                        size_t rd = fread(tail, 1, tail_len, rf);
                        tail[rd] = '\0';
                        fclose(rf);
                        /* 跳过残行，保证 JSONL 每行完整 */
                        char *first_nl = strchr(tail, '\n');
                        FILE *wf = fopen(SESSION_FILE, "w");
                        if (wf && first_nl) {
                            fwrite(first_nl + 1, 1, strlen(first_nl + 1), wf);
                            fclose(wf);
                        } else if (wf) fclose(wf);
                        free(tail);
                    } else fclose(rf);
                }
            }
        }
    }
    if (!sess_active) session_30s = 0;
}

/* ★ v1.5.0: 读 session.jsonl 尾部的最近 N 条，输出 JSON 数组
   （供 App 画充电曲线：电量/电流/电压/温度/阶段 随时间）
   max_lines 上限 2000，防止一次拉爆内存。 */
static char *session_json(int max_lines) {
    FILE *f = fopen(SESSION_FILE, "r");
    if (!f) return strdup("[]");
    fseek(f, 0, SEEK_END); long sz = ftell(f); rewind(f);
    if (sz <= 0) { fclose(f); return strdup("[]"); }
    /* 每行约 90 字节，只回读够用的尾部 */
    long want = (long)max_lines * 100;
    long start = (sz > want) ? sz - want : 0;
    fseek(f, start, SEEK_SET);
    char *buf = malloc(sz - start + 1);
    if (!buf) { fclose(f); return strdup("[]"); }
    size_t rd = fread(buf, 1, sz - start, f); fclose(f);
    buf[rd] = '\0';
    /* start 落在行中间时跳过残行 */
    char *p0 = buf;
    if (start > 0) {
        char *nl = strchr(buf, '\n');
        if (nl) p0 = nl + 1;
    }
    int nl_count = 0;
    for (char *c = p0; *c; c++) if (*c == '\n') nl_count++;
    int skip = nl_count > max_lines ? nl_count - max_lines : 0;
    char *startp = p0; int seen = 0;
    for (char *c = p0; *c && seen < skip; c++) if (*c == '\n') { startp = c + 1; seen++; }
    for (char *c = startp; *c; c++) if (*c == '\n') *c = ',';
    size_t blen = strlen(startp);
    if (blen > 0 && startp[blen-1] == ',') startp[blen-1] = '\0', blen--;
    char *out = malloc(blen + 3);
    if (!out) { free(buf); return strdup("[]"); }
    out[0] = '[';
    memcpy(out + 1, startp, blen);
    out[blen + 1] = ']'; out[blen + 2] = '\0';
    free(buf);
    return out;
}

static void handle_curve(int fd, const char *body) {
    (void)body;
    char *j = session_json(1440);   /* 30s 采样 → 720 条/6h；1440 上限约 12h */
    send_resp(fd, 200, "application/json", j);
    free(j);
}

static void handle_sessions(int fd, const char *body) {
    (void)body;
    /* 从 session.jsonl 里按时间间隔 >30min 切分，生成会话索引 */
    FILE *f = fopen(SESSION_FILE, "r");
    if (!f) { send_resp(fd, 200, "application/json", "[]"); return; }
    char line[512];
    long prev_t = 0, seg_start = 0;
    int seg_cap0 = -1, seg_mah = 0, seg_n = 0, seg_capmax = -1;
    char *out = malloc(16384);
    if (!out) { fclose(f); send_resp(fd, 200, "application/json", "[]"); return; }
    int off = 0;
    off += snprintf(out + off, 16384 - off, "[");
    int first = 1;
    while (fgets(line, sizeof(line), f)) {
        long t = 0; int c = -1, mah = 0;
        char *q = strstr(line, "\"t\":");
        if (q) t = atol(q + 4);
        q = strstr(line, "\"c\":");
        if (q) c = atoi(q + 4);
        q = strstr(line, "\"mah\":");
        if (q) mah = atoi(q + 6);
        if (t == 0) continue;
        if (prev_t && t - prev_t > 1800) {   /* 间隔 >30min 视为新会话 */
            if (seg_n > 2 && off < 16000)
                off += snprintf(out + off, 16384 - off,
                    "%s{\"t\":%ld,\"dur\":%ld,\"cap0\":%d,\"capmax\":%d,\"mah\":%d,\"n\":%d}",
                    first ? "" : ",", seg_start, prev_t - seg_start,
                    seg_cap0, seg_capmax, seg_mah, seg_n);
            first = 0;
            seg_start = t; seg_cap0 = c; seg_capmax = c; seg_mah = mah; seg_n = 1;
        } else {
            if (seg_n == 0) { seg_start = t; seg_cap0 = c; }
            if (c > seg_capmax) seg_capmax = c;
            seg_mah = mah; seg_n++;
        }
        prev_t = t;
    }
    if (seg_n > 2 && off < 16000)
        off += snprintf(out + off, 16384 - off,
            "%s{\"t\":%ld,\"dur\":%ld,\"cap0\":%d,\"capmax\":%d,\"mah\":%d,\"n\":%d}",
            first ? "" : ",", seg_start, prev_t - seg_start,
            seg_cap0, seg_capmax, seg_mah, seg_n);
    fclose(f);
    snprintf(out + off, 16384 - off, "]");
    send_resp(fd, 200, "application/json", out);
    free(out);
}

static void handle_calibrate(int fd, const char *body, const char *method) {
    if (!strcmp(method, "POST") && body) {
        if (strstr(body, "start"))      cal_start();
        else if (strstr(body, "abort")) cal_abort("用户主动中止");
    }
    char *j = cal_status_json();
    send_resp(fd, 200, "application/json", j);
    free(j);
}

static char *history_json(void) {
    FILE *f = fopen(HISTORY, "r"); if (!f) return strdup("[]");
    fseek(f, 0, SEEK_END); long sz = ftell(f); rewind(f);
    if (sz <= 0 || sz > (1<<20)) { fclose(f); return strdup("[]"); }
    char *buf = malloc(sz + 1);
    if (!buf) { fclose(f); return strdup("[]"); }   /* ★ NULL 检查 */
    size_t rd = fread(buf, 1, sz, f); fclose(f);
    buf[rd] = '\0';
    int nl = 0; for (char *s = buf; *s; s++) if (*s == '\n') nl++;
    int keep = nl > HIST_MAX ? nl - HIST_MAX : 0;
    char *start = buf; int seen = 0;
    for (char *s = buf; *s && seen < keep; s++) if (*s == '\n') { start = s+1; seen++; }
    for (char *p = start; *p; p++) if (*p == '\n') *p = ',';
    size_t blen = strlen(start);
    if (blen > 0 && start[blen-1] == ',') start[blen-1] = '\0', blen--;
    char *out = malloc(blen + 3);
    if (!out) { free(buf); return strdup("[]"); }   /* ★ NULL 检查 */
    out[0] = '[';
    memcpy(out+1, start, blen); out[blen+1] = ']'; out[blen+2] = '\0';
    free(buf); return out;
}

/* ★ v1.3.1 P2-16: 读系统 charge_logger CSV 最后一行（含 SOH/内阻/循环，比自研历史强） */
static int read_charge_logger_tail(char *out, int outsz) {
    const char *files[] = {"/data/vendor/charge_logger/charge_logger_a.csv",
                           "/data/vendor/charge_logger/charge_logger_b.csv", NULL};
    for (int i = 0; files[i]; i++) {
        FILE *f = fopen(files[i], "r");
        if (!f) continue;
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        long off = sz > 2048 ? sz - 2048 : 0;
        fseek(f, off, SEEK_SET);
        char line[512], last[512] = "";
        while (fgets(line, sizeof(line), f)) {
            if (strlen(line) > 20) strncpy(last, line, sizeof(last)-1);
        }
        fclose(f);
        if (last[0]) {
            char *nl = strchr(last, '\n'); if (nl) *nl = 0;
            snprintf(out, outsz, "%s", last);
            return 0;
        }
    }
    return -1;
}

static void send_resp(int fd, int code, const char *ctype, const char *body) {
    char head[512];
    const char *reason = code==200?"OK":code==405?"Method Not Allowed":code==404?"Not Found":code==500?"Internal Server Error":"Error";
    int n = snprintf(head, sizeof(head),
        "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
        "Connection: close\r\nAccess-Control-Allow-Origin: *\r\n\r\n",
        code, reason, ctype, body?strlen(body):0);
    ssize_t _w = write(fd, head, n); (void)_w;
    if (body) { ssize_t _w2 = write(fd, body, strlen(body)); (void)_w2; }
}

static void handle_status(int fd) {
    /* ★ v1.9.0: 每核频率数组预生成（App CPU 页需要逐核数据） */
    char cores_json[1024];
    {
        int off = 0;
        off += snprintf(cores_json + off, sizeof(cores_json) - off, "[");
        int n = cpu_cores > 0 && cpu_cores <= MAX_CORES ? cpu_cores : 1;
        for (int i = 0; i < n && off < (int)sizeof(cores_json) - 80; i++) {
            off += snprintf(cores_json + off, sizeof(cores_json) - off,
                "%s{\"f\":%d,\"max\":%d,\"min\":%d}",
                i ? "," : "", cpu_freq_khz[i], cpu_freq_max_khz[i], cpu_freq_min_khz[i]);
        }
        snprintf(cores_json + off, sizeof(cores_json) - off, "]");
    }

    /* ★ v1.6.0: 分区数组预生成（主 JSON 串用 %s 拼入） */
    char parts_json[1024];
    {
        int off = 0;
        off += snprintf(parts_json + off, sizeof(parts_json) - off, "[");
        for (int i = 0; i < part_count && off < (int)sizeof(parts_json) - 120; i++) {
            off += snprintf(parts_json + off, sizeof(parts_json) - off,
                "%s{\"m\":\"%s\",\"t\":%ld,\"f\":%ld}",
                i ? "," : "", part_mount[i], part_total_mb[i], part_free_mb[i]);
        }
        snprintf(parts_json + off, sizeof(parts_json) - off, "]");
    }
    /* ★ v1.8.0: status JSON 字段已增至 200+，8KB 栈缓冲区不够。
       改为堆分配 32KB（避免栈溢出，也消除 GCC 截断告警）。 */
    char *body = malloc(32768);
    if (!body) { send_resp(fd, 500, "text/plain", "oom"); return; }
    char cl_buf[512] = "";
    read_charge_logger_tail(cl_buf, sizeof(cl_buf));
    json_escape(cl_buf, cl_buf, sizeof(cl_buf));
    update_temps();
    read_pd_and_mtk();
    int charge_full = read_int(SYSFS "/charge_full") / 1000;

    /* 自身进程信息 */
    proc_pid_val = getpid();
    FILE *sf = fopen("/proc/self/stat", "r");
    if (sf) {
        char sb[512]; if (fgets(sb, sizeof(sb), sf)) {
            char *p = strrchr(sb, ')');
            if (p) {
                p++; while (*p == ' ') p++;
                long ut = 0, st = 0;
                sscanf(p, "%*c %*d %*d %*d %*d %*d %*d %*d %*d %*d %*d %ld %ld", &ut, &st);
                long now_pt = ut + st;
                FILE *tf = fopen("/proc/stat", "r");
                if (tf) { char tb[256]; if (fgets(tb, sizeof(tb), tf)) {
                    long u, n, s, i, w;
                    if (sscanf(tb, "cpu  %ld %ld %ld %ld %ld", &u, &n, &s, &i, &w) >= 5) {
                        long now_ct = u+n+s+i+w;
                        if (prev_proc_ticks > 0) {
                            long pd = now_pt - prev_proc_ticks;
                            long cd = now_ct - prev_cpu_ticks;
                            if (cd > 0) cpu_pct = (int)(pd * 1000 / cd);
                        }
                        prev_cpu_ticks = now_ct;
                    }
                } fclose(tf); }
                prev_proc_ticks = now_pt;
                char *s0 = strchr(sb, '('); char *s1 = strrchr(sb, ')');
                if (s0 && s1 && s1 > s0) {
                    int l = s1 - s0 - 1; if (l > 31) l = 31;
                    strncpy(proc_name_buf, s0+1, l); proc_name_buf[l] = '\0';
                }
            }
        }
        fclose(sf);
    }
    /* bypass 状态（实时读节点值）*/
    int bypass_ok = 1, bypass_on = 0;
    if (bypass_supported) {
        int bv = read_int(bypass_node);
        if (bv > 0) bypass_on = 1;
    } else bypass_on = bypass_soft;
    int power_mw = 0;
    /* 实时读取电流/电压（不依赖缓存）*/
    int cur_raw = read_int(SYSFS "/current_now");
    int vol_raw = read_int(SYSFS "/voltage_now");
    int cur_ma = cur_raw / 1000;
    int vol_mv = vol_raw / 1000;
    /* usb 输入端（5s 缓存）优先 */
    int usb_curr = usb_curr_cache;
    int usb_volt = usb_volt_cache;
    if (usb_curr > 0 && usb_volt > 0) {
        power_mw = (int)(((long)usb_curr / 1000) * ((long)usb_volt / 1000) / 1000);
        if (strcmp(bat_status, "Charging") != 0) power_mw = -power_mw;
    } else if (cur_ma != 0 && vol_mv > 0) {
        /* 电池侧：电流负=充电 → 功率正；电流正=放电 → 功率负 */
        power_mw = ((long)vol_mv * (cur_ma < 0 ? -cur_ma : cur_ma)) / 1000;
        if (cur_ma > 0) power_mw = -power_mw;
    }
    int cap_raw = read_int("/sys/class/power_supply/bms/capacity_raw");
    int capacity_disp = cap_raw > 0 ? cap_raw : bat_capacity * 100;
    int hw_paused = read_int(SYSFS "/input_suspend");
    if (hw_paused < 0) hw_paused = read_int(SYSFS_USB "/input_suspend");
    if (hw_paused < 0) hw_paused = 0;
    int est_full_min = 0;
    if (power_mw > 0 && charge_limit > bat_capacity && charge_full > 0) {
        int remain = ((charge_limit - bat_capacity) * charge_full) / 100;
        int cmA = power_mw * 1000 / (vol_mv > 0 ? vol_mv : 4000);
        if (cmA > 0) est_full_min = (remain * 60) / cmA;
    }
    /* 充满预估文本 + 健康衰减率 */
    if (est_full_min > 0) {
        int h = est_full_min / 60, m = est_full_min % 60;
        if (h > 0 && m > 0) snprintf(est_full_txt, sizeof(est_full_txt), "%d小时%d分", h, m);
        else if (h > 0) snprintf(est_full_txt, sizeof(est_full_txt), "%d小时", h);
        else snprintf(est_full_txt, sizeof(est_full_txt), "%d分", m);
    } else strcpy(est_full_txt, "—");
    health_degrade = 0.0f;
    if (cycle_count > 0 && health > 0 && health < 100)
        health_degrade = (100.0f - health) / cycle_count * 1000.0f; /* 每千次循环衰减% */
    const char *health_rating = "未知";
    if (health >= 90) health_rating = "优秀";
    else if (health >= 80) health_rating = "良好";
    else if (health >= 70) health_rating = "一般";
    else if (health >= 0) health_rating = "较差";
    int est_cycles_left = 0;
    if (cycle_count > 0 && health > 0) {
        float loss_per_cycle = (100.0f - health) / cycle_count;
        if (loss_per_cycle > 0.001f)
            est_cycles_left = (int)((health - 60.0f) / loss_per_cycle);
    }
    snprintf(body, 32768,
        "{\"capacity\":%d,\"capacity_disp\":%d,\"temp\":%d,\"voltage\":%d,\"current\":%d,"
        "\"status\":\"%s\",\"limit_enabled\":%d,\"charge_limit\":%d,"
        "\"temp_limit\":%d,\"resume_delta\":%d,\"interval\":%d,"
        "\"soc_temp\":%d,\"gpu_temp\":%d,\"chg_temp\":%d,\"case_temp\":%d,"
        "\"cycle_count\":%d,\"health\":%d,\"health_rating\":\"%s\","
        "\"charge_full_mah\":%d,\"est_cycles_left\":%d,"
        "\"power_mw\":%d,\"est_full_min\":%d,\"est_full_txt\":\"%s\",\"health_degrade\":%.1f,"
        "\"usb_online\":%d,\"proto_name\":\"%s\",\"pd_type\":%d,\"power_max\":%d,"
        "\"control_state\":\"%s\",\"full_once\":%d,\"full_until\":%ld,"
        "\"history_enabled\":%d,\"paused\":%d,\"proc_name\":\"%s\",\"proc_pid\":%d,\"cpu_pct\":%d,"
        "\"mem_total\":%d,\"mem_avail\":%d,\"mem_free\":%d,\"storage_total\":%d,\"storage_free\":%d,\"uptime_secs\":%d,\"cpu_cores\":%d,\"kernel_ver\":\"%s\",\"cpu_model\":\"%s\",\"device_model\":\"%s\","
        /* v1.6.0 系统字段 */
        "\"cpu_usage\":%d,\"cpu_idle\":%d,\"cpu_deep_sleep\":%d,\"cpu_arch\":\"%s\",\"cpu_abi\":\"%s\","
        "\"cpu_cache_l1\":\"%s\",\"cpu_cache_l2\":\"%s\",\"cpu_cache_l3\":\"%s\","
        "\"cpu_freq_khz\":%d,\"cpu_freq_max_khz\":%d,\"cpu_freq_min_khz\":%d,\"cores\":%s,"
        "\"mem_buffers\":%d,\"mem_cached\":%d,\"mem_sreclaim\":%d,\"mem_sunreclaim\":%d,\"mem_shmem\":%d,"
        "\"mem_pagetables\":%d,\"mem_kernelstack\":%d,\"mem_vmallocused\":%d,\"mem_slab\":%d,\"mem_mapped\":%d,"
        "\"mem_swap_total\":%d,\"mem_swap_free\":%d,\"mem_zram_orig\":%d,\"mem_zram_compr\":%d,\"mem_zram_used\":%d,"
        "\"sys_sdk_level\":%d,\"sys_android_ver\":\"%s\",\"sys_build_display\":\"%s\",\"sys_security_patch\":\"%s\","
        "\"sys_bootloader\":\"%s\",\"sys_soc_vendor\":\"%s\",\"sys_soc_model\":\"%s\",\"part_count\":%d,\"parts\":%s,"
        /* v1.6.0 GPU */
        "\"gpu_model\":\"%s\",\"gpu_vendor\":\"%s\",\"gpu_freq_khz\":%d,\"gpu_freq_max_khz\":%d,\"gpu_freq_min_khz\":%d,"
        "\"gpu_governor\":\"%s\",\"gpu_avail_gov\":\"%s\",\"gpu_load\":%d,\"gpu_load_src\":\"%s\",\"gpu_throttled\":%d,"
        "\"gpu_temp_c\":%d,"
        /* v1.7.0 OEM */
        "\"oem_brand\":\"%s\",\"oem_os_name\":\"%s\",\"oem_os_version\":\"%s\","
        "\"oem_fingerprint\":\"%s\",\"oem_charge_proto\":\"%s\",\"oem_charge_watt\":\"%s\","
        "\"oem_bat_capacity\":\"%s\",\"oem_dual_cell\":%d,\"oem_display_panel\":\"%s\","
        "\"oem_display_vendor\":\"%s\",\"oem_display_max_hz\":\"%s\",\"oem_display_nits\":\"%s\","
        "\"oem_cpu_governor\":\"%s\",\"oem_gpu_governor\":\"%s\",\"oem_security_chip\":\"%s\","
        "\"oem_secure_boot\":%d,\"oem_verified_boot\":\"%s\",\"oem_game_mode\":\"%s\","
        "\"oem_perf_mode\":\"%s\",\"oem_power_save\":\"%s\",\"oem_ultra_power_save\":\"%s\","
        "\"oem_ai_engine\":\"%s\",\"oem_memory_fusion\":\"%s\",\"oem_thermal\":\"%s\","
        "\"oem_storage_boost\":\"%s\","
        /* v1.8.0 硬件 / 设备详情 */
        "\"dev_storage_type\":\"%s\",\"dev_memory_type\":\"%s\",\"dev_mem_speed_mhz\":%d,"
        "\"dev_soc_chipname\":\"%s\",\"dev_soc_process\":\"%s\",\"dev_usb_version\":\"%s\","
        "\"dev_usb_typec\":%d,\"dev_usb_host\":%d,\"dev_bt_ver\":\"%s\","
        "\"dev_wifi_standard\":\"%s\",\"dev_wifi_6e\":%d,\"dev_wifi_ax\":%d,\"dev_wifi_be\":%d,"
        "\"dev_widevine\":\"%s\",\"dev_selinux\":\"%s\",\"dev_crypto_state\":\"%s\",\"dev_crypto_type\":\"%s\","
        "\"dev_tee_supported\":%d,\"dev_tee_ver\":\"%s\",\"dev_bootloader_locked\":%d,"
        "\"dev_openssl\":\"%s\",\"dev_gles\":\"%s\",\"dev_vulkan_level\":\"%s\",\"dev_hw_egl\":\"%s\","
        "\"dev_serial\":\"%s\",\"dev_display_type\":\"%s\",\"dev_display_bit\":\"%s\","
        "\"dev_display_gamut\":\"%s\",\"dev_display_series\":\"%s\",\"dev_audio_hires\":\"%s\","
        "\"dev_dual_sim\":%d,\"dev_sim_operator\":\"%s\","
        "\"dev_has_nfc\":%d,\"dev_has_ir\":%d,\"dev_has_uwb\":%d,\"dev_has_wireless_chg\":%d,"
        "\"dev_screen_w\":%d,\"dev_screen_h\":%d,\"dev_screen_density\":%d,"
        "\"dev_thermal_zones\":%d,\"dev_thermal_types\":\"%s\","
        "\"wifi_rssi\":%d,\"wifi_on\":%d,\"mobile_on\":%d,\"ip_addr\":\"%s\",\"bat_technology\":\"%s\",\"bat_capacity_level\":\"%s\",\"bat_health\":\"%s\",\"charge_type\":%d,\"time_to_full\":%d,\"charge_counter\":%d,\"input_current_limit\":%d,\"bat_manufacturer\":\"%s\",\"bat_model_name\":\"%s\",\"voltage_ocv\":%d,\"current_avg\":%d,\"temp_ambient\":%d,\"constant_charge_current\":%d,\"constant_charge_voltage\":%d,\"capacity_error_margin\":%d,\"time_to_empty\":%d,\"bat_present\":%d,\"internal_resistance\":%d,\"charge_now\":%d,\"charge_term_current\":%d,\"constant_charge_current_max\":%d,\"constant_charge_voltage_max\":%d,\"temp_max\":%d,\"temp_min\":%d,\"charger_temp\":%d,\"charge_done\":%d,\"input_voltage_settled\":%d,\"safety_timer_expired\":%d,\"calibrate\":%d,\"webhook\":\"%s\",\"bypass_ok\":%d,\"bypass_on\":%d,\"bypass_hw\":%d,\"sess_active\":%d,\"sess_min\":%d,\"sess_mah\":%d,"
        "\"stats_count\":%d,\"stats_min\":%ld,\"stats_mah\":%ld,\"night\":%d,\"scene\":%d,\"lang\":%d,\"theme\":%d,\"alerted\":%d,\"pd_pdo_count\":%d,\"pd_pdo_max_mv\":%d,\"pd_pdo_max_ma\":%d,\"pd_svid\":\"%s\",\"pd_state\":\"%s\",\"cc_limit_supported\":%d,\"cc_limit_val\":%d,\"power_path_en\":%d,\"jeita_index\":%d,\"soh_native\":%d,\"soh_first\":%d,\"soh_delta_x10\":%d,\"design_mah\":%d,\"chg_temp_limit\":%d,\"x_pp\":%d,\"x_pp_ok\":%d,\"x_cv\":%d,\"x_cv_mv\":%d,\"x_cv_ok\":%d,\"x_lc\":%d,\"x_lc_ma\":%d,\"x_nn\":%d,\"x_nn_ok\":%d,\"x_bmin\":%d,\"native_night\":%d,\"cl_tail\":\"%s\"}",
        bat_capacity, capacity_disp, bat_temp, vol_mv, cur_ma, bat_status,
        limit_enabled, charge_limit, temp_limit, resume_delta, interval,
        soc_temp, gpu_temp, chg_temp, case_temp,
        cycle_count, health, health_rating,
        charge_full, est_cycles_left,
        power_mw, est_full_min, est_full_txt, health_degrade,
        usb_online, proto_name, pd_type, usb_power_max,
        control_state, full_once, (long)full_until, history_enabled, hw_paused, proc_name_buf, proc_pid_val, cpu_pct,
        mem_total, mem_avail, mem_free, storage_total, storage_free, uptime_secs, cpu_cores, kernel_ver, cpu_model, device_model,
        /* ★ v1.6.0 新增系统字段 */
        cpu_usage_total, cpu_idle_pct, cpu_deep_sleep_pct, cpu_arch, cpu_abi,
        cpu_cache_l1, cpu_cache_l2, cpu_cache_l3,
        cpu_freq_khz[0], cpu_freq_max_khz[0], cpu_freq_min_khz[0], cores_json,
        mem_buffers, mem_cached, mem_sreclaim, mem_sunreclaim, mem_shmem,
        mem_pagetables, mem_kernelstack, mem_vmallocused, mem_slab, mem_mapped,
        mem_swap_total, mem_swap_free, mem_zram_orig, mem_zram_compr, mem_zram_used,
        sys_sdk_level, sys_android_ver, sys_build_display, sys_security_patch,
        sys_bootloader, sys_soc_vendor, sys_soc_model, part_count, parts_json,
        gpu_model, gpu_vendor, gpu_freq_khz, gpu_freq_max_khz, gpu_freq_min_khz,
        gpu_governor, gpu_avail_gov, gpu_load_pct, gpu_load_src, gpu_throttled,
        gpu_temp_c,
        oem_brand, oem_os_name, oem_os_version,
        oem_build_fingerprint, oem_charging_protocol, oem_charging_watt,
        oem_battery_capacity, oem_dual_cell, oem_display_panel,
        oem_display_vendor, oem_display_max_hz, oem_display_peak_nits,
        oem_cpu_governor, oem_gpu_governor, oem_security_chip,
        oem_secure_boot, oem_verified_boot, oem_game_mode,
        oem_perf_mode, oem_power_save, oem_ultra_power_save,
        oem_ai_engine, oem_memory_fusion, oem_thermal_solution,
        oem_storage_boost,
        dev_storage_type, dev_memory_type, dev_memory_speed_mhz,
        dev_soc_chipname, dev_soc_process_node, dev_usb_version,
        dev_usb_typec, dev_usb_host, dev_bluetooth_ver,
        dev_wifi_standard, dev_wifi_6e, dev_wifi_ax, dev_wifi_be,
        dev_widevine_level, dev_selinux, dev_crypto_state, dev_crypto_type,
        dev_tee_supported, dev_tee_version, dev_bootloader_locked,
        dev_openssl, dev_gles_version, dev_vulkan_level, dev_hardware_egl,
        dev_serial, dev_display_type, dev_display_bit,
        dev_display_gamut, dev_display_series, dev_audio_hires,
        dev_dual_sim, dev_sim_operator,
        dev_has_nfc, dev_has_ir, dev_has_uwb, dev_has_wireless_chg,
        dev_screen_w, dev_screen_h, dev_screen_density,
        dev_thermal_zones, dev_thermal_types, wifi_rssi, wifi_on, mobile_on, ip_addr, bat_technology, bat_capacity_level, bat_health, bat_charge_type, bat_time_to_full, bat_charge_counter, bat_input_current_limit, bat_manufacturer, bat_model_name, voltage_ocv, current_avg, temp_ambient, constant_charge_current, constant_charge_voltage, capacity_error_margin, time_to_empty, bat_present, internal_resistance, charge_now, charge_term_current, constant_charge_current_max, constant_charge_voltage_max, temp_max, temp_min, charger_temp, charge_done, input_voltage_settled, safety_timer_expired, calibrate, webhook_url, bypass_ok, bypass_on, bypass_supported, sess_active, sess_min, sess_mah, stats_count, stats_min, stats_mah, night_enabled, scene, lang, theme, alerted_high,
        pd_pdo_count, pd_pdo_max_mv, pd_pdo_max_ma, pd_svid, pd_state,
        cc_limit_supported, cc_limit_val, power_path_en, jeita_index,
        soh_native, soh_first, soh_delta_x10, design_mah, chg_temp_limit,
        x_en_power_path, (access("/proc/mtk_battery_cmd/en_power_path", W_OK) == 0),
        x_set_cv_en, x_set_cv_mv, (access("/proc/mtk_battery_cmd/set_cv", W_OK) == 0),
        x_limit_current_en, x_limit_current_ma,
        x_use_native_night, (access(SYSFS "/night_charging", W_OK) == 0),
        x_bypass_min_soc,
        read_int(SYSFS "/night_charging"),
        cl_buf);
    /* 系统信息 */
    { int v = 0;
        FILE *mf = fopen("/proc/meminfo", "r");
        if (mf) {
            char ml[256];
            while (fgets(ml, sizeof(ml), mf)) {
                if (sscanf(ml, "MemTotal: %d kB", &v) == 1) mem_total = v;
                else if (sscanf(ml, "MemAvailable: %d kB", &v) == 1) mem_avail = v;
                else if (sscanf(ml, "MemFree: %d kB", &v) == 1) mem_free = v;
            }
            fclose(mf);
        }
        struct statfs sf;
        if (statfs("/data", &sf) == 0) {
            long long total = (long long)sf.f_blocks * sf.f_bsize / (1024*1024);
            long long free = (long long)sf.f_bfree * sf.f_bsize / (1024*1024);
            storage_total = (int)total; storage_free = (int)free;
        }
        FILE *uf = fopen("/proc/uptime", "r");
        if (uf) { double up; if (fscanf(uf, "%lf", &up) == 1) uptime_secs = (int)up; fclose(uf); }
        /* CPU 核心数 */
        FILE *cf = fopen("/sys/devices/system/cpu/present", "r");
        if (cf) { int a, b; if (fscanf(cf, "%d-%d", &a, &b) == 2) cpu_cores = b - a + 1; fclose(cf); }
        if (cpu_cores < 0) cpu_cores = 1;
        /* 内核版本 */
        FILE *vf = fopen("/proc/version", "r");
        if (vf) { if (fgets(kernel_ver, sizeof(kernel_ver), vf)) {}
            char *nl = strchr(kernel_ver, '\n'); if (nl) *nl = 0;
            fclose(vf); }
        /* 清理 kernel_ver 到只保留版本号部分 */
        char *sp = strstr(kernel_ver, "version ");
        if (sp) { memmove(kernel_ver, sp + 8, strlen(sp) - 7); }
    }
    send_resp(fd, 200, "application/json", body);
    free(body);
}

static void handle_limit(int fd, const char *body) {
    if (body) {
        char *copy = strdup(body); char *save = NULL;
        if (!copy) { send_resp(fd, 200, "application/json", "{\"ok\":false}"); return; }
        for (char *tok = strtok_r(copy, "&", &save); tok; tok = strtok_r(NULL, "&", &save)) {
            char *eq = strchr(tok, '='); if (!eq) continue;
            *eq = '\0'; char key[64], val[512];
            strncpy(key, tok, 63); key[63] = '\0';
            strncpy(val, eq+1, 511); val[511] = '\0';
            const char *dec = val;
            if (!strcmp(key, "charge_limit")) { int x = atoi(dec); if (x >= 20 && x <= 100) charge_limit = x; }
            else if (!strcmp(key, "temp_limit")) { int x = atoi(dec); if (x >= 30 && x <= 60) temp_limit = x; }
            else if (!strcmp(key, "chg_temp_limit")) { int x = atoi(dec); if (x >= 40 && x <= 90) chg_temp_limit = x; }
            else if (!strcmp(key, "resume_delta")) { int x = atoi(dec); if (x >= 1 && x <= 20) resume_delta = x; }
            else if (!strcmp(key, "interval")) { int x = atoi(dec); if (x >= 1 && x <= 60) interval = x; }
            else if (!strcmp(key, "enabled")) limit_enabled = atoi(dec);
            else if (!strcmp(key, "history_enabled")) history_enabled = atoi(dec);
            else if (!strcmp(key, "bypass")) {
                if (bypass_supported) write_str(bypass_node, atoi(dec) ? "1" : "0");
                else { bypass_soft = atoi(dec) ? 1 : 0; if (bypass_soft) last_suspend_val = -1; }
            }
            else if (!strcmp(key, "night")) night_enabled = atoi(dec);
            else if (!strcmp(key, "ns")) { int x = atoi(dec); if (x >= 0 && x <= 23) night_start_h = x; }
            else if (!strcmp(key, "ne")) { int x = atoi(dec); if (x >= 0 && x <= 23) night_end_h = x; }
            else if (!strcmp(key, "scene")) {
                scene = atoi(dec);
                if (scene == 1) { charge_limit = 70; temp_limit = 40; log_event("scene: gaming"); }
                else if (scene == 2) { night_enabled = 1; log_event("scene: sleep"); }
                else if (scene == 3) { paused = 1; log_event("scene: trip pause"); }
                else if (scene == 0) { paused = 0; }
            }
            else if (!strcmp(key, "lang")) lang = atoi(dec);
            else if (!strcmp(key, "theme")) theme = atoi(dec);
            /* ★ v1.3.1 P2 新功能开关
               ★ v1.4.0: 同时接受 conf 里的短名（xpp/xcv/...）。
               原先 API 只认长名(x_set_cv_en)，conf 却存短名(xcv)，
               重启后 load_config 用 xcv=0 覆盖掉刚设的 x_set_cv_en=1，
               限流/恒压点/旁路在重启后会静默失效。 */
            else if (!strcmp(key, "x_power_path") || !strcmp(key, "xpp")) x_en_power_path = atoi(dec) ? 1 : 0;
            else if (!strcmp(key, "x_set_cv_en") || !strcmp(key, "xcv")) x_set_cv_en = atoi(dec) ? 1 : 0;
            else if (!strcmp(key, "x_set_cv_mv") || !strcmp(key, "xcvmv")) { int v = atoi(dec); if (v >= 3900 && v <= 4500) x_set_cv_mv = v; }
            else if (!strcmp(key, "x_limit_en") || !strcmp(key, "xcc")) x_limit_current_en = atoi(dec) ? 1 : 0;
            else if (!strcmp(key, "x_limit_ma") || !strcmp(key, "xccma")) { int v = atoi(dec); if (v > 0 && v <= 6000) x_limit_current_ma = v; }
            else if (!strcmp(key, "x_native_night") || !strcmp(key, "xnight")) x_use_native_night = atoi(dec) ? 1 : 0;
            else if (!strcmp(key, "x_bypass_min_soc") || !strcmp(key, "xminsoc")) { int v = atoi(dec); if (v >= 20 && v <= 90) x_bypass_min_soc = v; }
            else if (!strcmp(key, "webhook")) {
                char wb[256]; strncpy(wb, dec, 255); wb[255] = '\0';
                url_decode(wb);
                if (strlen(wb) < 255 && !strchr(wb, '\"') && !strchr(wb, '\\'))
                    strcpy(webhook_url, wb);
                else log_event("webhook invalid chars");
            }
        }
        save_extras();
        free(copy);
        save_config(); apply_control(); apply_power_path(); apply_cv_and_current(); apply_native_night(); apply_night_mode(); high_temp_alert();
        log_event("配置已更新");
    }
    send_resp(fd, 200, "application/json", "{\"ok\":true}");
}

static void handle_full(int fd, const char *body) {
    if (body && strstr(body, "cancel=1")) {
        full_once = 0;
        log_event("手动充满已取消");
    } else {
        full_once = 1; full_until = time(NULL) + FULL_TIMEOUT;
        log_event("手动充满已启动，30分钟超时");
    }
    apply_control();
    send_resp(fd, 200, "application/json", "{\"ok\":true}");
}

static void handle_pause(int fd, const char *body) {
    if (body && strstr(body, "pause=1")) {
        paused = 1;
        log_event("manual pause");
    } else {
        paused = 0;
        log_event("manual resume");
        /* 恢复后立即写 0（中间地带 apply_control 不会写，避免卡死）*/
        if (bat_temp < temp_limit)
            write_str(SYSFS "/input_suspend", "0");
    }
    apply_control();
    send_resp(fd, 200, "application/json", "{\"ok\":true}");
}


/* ★ v1.3.1: HTTP 客户端探测（Android 无 curl，优先 busybox wget / toybox） */
static const char *http_tool(void) {
    static char cached[128] = "";
    if (cached[0]) return cached;
    /* 候选路径（按优先级） */
    const char *cands[] = {
        "/data/adb/ap/bin/busybox wget -q -O -",
        "/data/adb/ksu/bin/busybox wget -q -O -",
        "/data/adb/magisk/busybox wget -q -O -",
        "busybox wget -q -O -",
        "wget -q -O -",
        "curl -s --max-time 8",
        NULL
    };
    for (int i = 0; cands[i]; i++) {
        char t[256];
        snprintf(t, sizeof(t), "%s --help >/dev/null 2>&1", cands[i]);
        /* 只做粗略探测：busybox 存在即认为可用 */
        if (strstr(cands[i], "busybox")) {
            if (access("/data/adb/ap/bin/busybox", X_OK) == 0 ||
                access("/data/adb/ksu/bin/busybox", X_OK) == 0 ||
                access("/data/adb/magisk/busybox", X_OK) == 0) {
                strncpy(cached, cands[i], sizeof(cached)-1);
                return cached;
            }
        } else {
            strncpy(cached, cands[i], sizeof(cached)-1);
            return cached;
        }
    }
    strcpy(cached, "");
    return cached;
}

/* 拼 HTTP GET 命令（自动选工具） */
static int http_get(const char *url, char *out, int outsz, int timeout) {
    const char *tool = http_tool();
    if (!tool[0]) return -1;
    char cmd[1024];
    if (strstr(tool, "busybox") || strstr(tool, "wget"))
        snprintf(cmd, sizeof(cmd), "%s -T %d '%s' 2>/dev/null", tool, timeout, url);
    else
        snprintf(cmd, sizeof(cmd), "%s --max-time %d '%s' 2>/dev/null", tool, timeout, url);
    FILE *f = popen(cmd, "r");
    if (!f) return -1;
    size_t n = fread(out, 1, outsz - 1, f);
    int st = pclose(f);
    out[n] = '\0';
    return (st == 0) ? (int)n : -1;
}

static int ver_cmp(const char *a, const char *b) {
    /* 跳过前缀，比较主.次.修订 三段 */
    while (*a && !isdigit(*a)) a++;
    while (*b && !isdigit(*b)) b++;
    for (int i = 0; i < 3; i++) {
        int va = atoi(a), vb = atoi(b);
        if (va != vb) return va - vb;
        a = strchr(a, '.'); b = strchr(b, '.');
        if (!a || !b) return 0;
        a++; b++;
    }
    return 0;
}

static void handle_update_check(int fd) {
    /* v1.3.1: 只做"检查"，不再自己下载。真正的更新由 APatch/KSU 管理器走 updateJson 渠道完成 */
    char buf[2048];
    int n = http_get("https://api.github.com/repos/ce11kjw/ChargeControl/releases/latest", buf, sizeof(buf), 8);
    if (n < 0) {
        /* 网络不可用：返回当前版本 + 提示走管理器 */
        send_resp(fd, 200, "application/json",
            "{\"update_available\":false,\"error\":\"network\",\"current\":\"" VERSION "\","
            "\"hint\":\"请在模块管理器(APatch/KernelSU)中检查更新\"}");
        return;
    }
    char *tag = strstr(buf, "\"tag_name\":\"");
    if (!tag) {
        send_resp(fd, 200, "application/json",
            "{\"update_available\":false,\"error\":\"parse\",\"current\":\"" VERSION "\"}");
        return;
    }
    tag += 12; char latest[32]; int i = 0;
    while (*tag && *tag != '"' && i < 31) latest[i++] = *tag++;
    latest[i] = '\0';
    int newer = ver_cmp(latest, VERSION);
    char resp[512];
    snprintf(resp, sizeof(resp),
        "{\"update_available\":%s,\"latest\":\"%s\",\"current\":\"%s\","
        "\"hint\":\"请在模块管理器(App)中更新，不要在此下载\"}",
        newer > 0 ? "true" : "false", latest, VERSION);
    send_resp(fd, 200, "application/json", resp);
}


static void handle_export(int fd) {
    char buf[8192]; int n = 0;
    n += snprintf(buf+n, sizeof(buf)-n, "=== ChargeControl 导出 ===\n时间: %ld\n\n--- 配置 ---\n", (long)time(NULL));
    FILE *f = fopen(CONFIG, "r");
    if (f) { char line[256]; while (n < (int)sizeof(buf)-256 && fgets(line, sizeof(line), f)) n += snprintf(buf+n, sizeof(buf)-n, "%s", line); fclose(f); }
    n += snprintf(buf+n, sizeof(buf)-n, "\n--- 日志 (尾部 4KB) ---\n");
    f = fopen(LOGFILE, "r");
    if (f) { fseek(f, 0, SEEK_END); long lsz = ftell(f); long off = lsz > 4096 ? lsz - 4096 : 0; fseek(f, off, SEEK_SET);
        char line[256]; while (n < (int)sizeof(buf)-256 && fgets(line, sizeof(line), f)) n += snprintf(buf+n, sizeof(buf)-n, "%s", line); fclose(f); }
    n += snprintf(buf+n, sizeof(buf)-n, "\n--- 历史 (最后20条) ---\n");
    f = fopen(HISTORY, "r");
    if (f) {
        char *lines[20]; int lc = 0;
        char line[512];
        while (n < (int)sizeof(buf)-512 && fgets(line, sizeof(line), f) && lc < 20) {
            lines[lc] = strdup(line); if (!lines[lc]) break; lc++;
        }
        fclose(f);
        for (int i = 0; i < lc; i++) {
            n += snprintf(buf+n, sizeof(buf)-n, "%s", lines[i]); free(lines[i]);
        }
    }
    char head[512];
    int hn = snprintf(head, sizeof(head),
        "HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\n"
        "Content-Disposition: attachment; filename=\"chargecontrol_export.txt\"\r\n"
        "Content-Length: %d\r\nConnection: close\r\nAccess-Control-Allow-Origin: *\r\n\r\n", n);
    ssize_t _w = write(fd, head, hn); (void)_w;
    ssize_t _we = write(fd, buf, n); (void)_we;
}

static int serve_file(int fd, const char *uri) {
    char path[512];
    if (!strcmp(uri, "/") || !strcmp(uri, "/index.html"))
        snprintf(path, sizeof(path), "%s/index.html", WEBROOT);
    else snprintf(path, sizeof(path), "%s%s", WEBROOT, uri);
    if (strstr(path, "..")) { send_resp(fd, 404, "text/plain", "Not Found"); return -1; }
    FILE *f = fopen(path, "rb"); if (!f) { send_resp(fd, 404, "text/plain", "Not Found"); return -1; }
    fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
    char *body = malloc(len+1);
    if (!body) { fclose(f); send_resp(fd, 500, "text/plain", "OOM"); return -1; }
    size_t rd = fread(body, 1, len, f); fclose(f);
    body[rd] = '\0';
    const char *ct = "text/plain";
    if (strstr(uri, ".html")) ct = "text/html";
    else if (strstr(uri, ".css")) ct = "text/css";
    else if (strstr(uri, ".js")) ct = "application/javascript";
    else if (strstr(uri, ".svg")) ct = "image/svg+xml";
    char head[512];
    int n = snprintf(head, sizeof(head),
        "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\nAccess-Control-Allow-Origin: *\r\n\r\n", ct, rd);
    ssize_t _w = write(fd, head, n); (void)_w;
    ssize_t _w3 = write(fd, body, rd); (void)_w3;
    free(body); return 0;
}

static void handle_client(int fd) {
    char buf[MAX_LINE]; int n = 0;
    /* ★ v1.3.1: 循环读，直到收到完整请求头（\r\n\r\n）或读满 */
    while (n < (int)sizeof(buf) - 1) {
        int r = read(fd, buf + n, sizeof(buf) - 1 - n);
        if (r <= 0) break;
        n += r;
        buf[n] = '\0';
        if (strstr(buf, "\r\n\r\n")) break;   /* 头部完整（body 跟随其后） */
    }
    if (n <= 0) return;
    buf[n] = '\0';
    char method[16], uri[256];
    if (sscanf(buf, "%15s %255s", method, uri) != 2 || strlen(uri) >= 255) return;
    char *body = NULL;
    if (!strcmp(method, "POST")) {
        char *hp = strstr(buf, "\r\n\r\n");
        if (hp) body = hp + 4;
    }
    if (!strcmp(uri, "/api/status")) { handle_status(fd); return; }
    if (!strcmp(uri, "/api/limit") && !strcmp(method, "POST")) { handle_limit(fd, body); return; }
    if (!strcmp(uri, "/api/full") && !strcmp(method, "POST")) { handle_full(fd, body); return; }
    if (!strcmp(uri, "/api/pause") && !strcmp(method, "POST")) { handle_pause(fd, body); return; }
    if (!strcmp(uri, "/api/log") && !strcmp(method, "GET")) {
        FILE *lf = fopen(LOGFILE, "r");
        if (!lf) { send_resp(fd, 200, "text/plain", "暂无日志"); return; }
        fseek(lf, 0, SEEK_END); long lsz = ftell(lf);
        long start = 0;
        if (lsz > 65536) { start = lsz - 65536; fseek(lf, start, SEEK_SET); lsz = 65536; }
        else fseek(lf, 0, SEEK_SET);
        char *lbuf = malloc(lsz+1);
        if (lbuf) {
            size_t lrd = fread(lbuf, 1, lsz, lf); fclose(lf);
            lbuf[lrd] = '\0'; send_resp(fd, 200, "text/plain", lbuf); free(lbuf);
        } else { fclose(lf); send_resp(fd, 200, "text/plain", "暂无日志"); }
        return;
    }
    if (!strcmp(uri, "/api/export")) { handle_export(fd); return; }
    if (!strcmp(uri, "/api/update-check")) { handle_update_check(fd); return; }
    /* v1.3.1: /api/update-apply 已移除（改用 APatch 管理器的 updateJson 渠道）*/
    if (!strcmp(uri, "/api/history")) { char *j = history_json(); send_resp(fd, 200, "application/json", j); free(j); return; }
    /* ★ v1.5.0 充电曲线 & 校准 */
    if (!strcmp(uri, "/api/curve"))     { handle_curve(fd, body); return; }
    if (!strcmp(uri, "/api/sessions"))  { handle_sessions(fd, body); return; }
    if (!strcmp(uri, "/api/calibrate")) { handle_calibrate(fd, body, method); return; }
    if (!strcmp(method, "GET")) serve_file(fd, uri);
    else send_resp(fd, 405, "text/plain", "Method Not Allowed");
}

static void on_sig(int sig) { (void)sig; running = 0; }

int main(void) {
    load_config(); load_stats(); load_extras(); last_save = time(NULL);
    update_battery();
    /* ★ v1.4.0: 抓住未被污染的原厂恒压点（务必在任何 apply_* 之前） */
    snapshot_orig_cv();
    load_cal_state();   /* ★ v1.5.0: 恢复校准状态机（重启不丢） */
    /* 探测旁路支持 */
    FILE *bf = popen("ls /sys/class/power_supply/*/bypass_charger /sys/class/power_supply/*/charge_bypass /sys/class/power_supply/*/bypass /sys/class/power_supply/*/bypass_mode /sys/class/power_supply/*/bypass_charging /sys/class/power_supply/*/charging_bypass /sys/class/power_supply/*/smb_bypass /sys/class/power_supply/*/main_bypass /sys/class/power_supply/*/batt_bypass /sys/class/power_supply/*/charge_control_bypass 2>/dev/null | head -1", "r");
    if (bf) { char bb[64]; if (fgets(bb, sizeof(bb), bf)) { size_t bl = strlen(bb); while (bl > 0 && (bb[bl-1]=='\n'||bb[bl-1]==' ')) bb[--bl]='\0'; if (bl>0) { bypass_supported = 1; strncpy(bypass_node, bb, sizeof(bypass_node)-1); } } pclose(bf); }
    log_event("ChargeControl 守护进程启动");

    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) return 1;
    int opt = 1; setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr; memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET; addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(PORT);
    if (bind(srv, (struct sockaddr*)&addr, sizeof(addr)) < 0) return 1;
    if (listen(srv, 8) < 0) return 1;
    signal(SIGINT, on_sig); signal(SIGTERM, on_sig); signal(SIGPIPE, SIG_IGN);

    time_t last_poll = 0;
    while (running) {
        time_t now = time(NULL);
        /* select 等待剩余时间，避免频繁唤醒 */
        int wait_sec = 1;
        if (now - last_poll < interval) {
            int remain = interval - (now - last_poll);
            if (remain > 0 && remain <= 5) wait_sec = remain;
        }
        fd_set rfds; FD_ZERO(&rfds); FD_SET(srv, &rfds);
        struct timeval tv = { wait_sec, 0 };
        int s = select(srv+1, &rfds, NULL, NULL, &tv);
        if (s < 0) { sleep(1); continue; }
        if (s > 0 && FD_ISSET(srv, &rfds)) {
            struct sockaddr_in cli; socklen_t cli_len = sizeof(cli);
            int cfd = accept(srv, (struct sockaddr*)&cli, &cli_len);
            if (cfd >= 0) { handle_client(cfd); close(cfd); }
        }
        now = time(NULL);
        if (now - last_poll >= (time_t)interval) {
            last_poll = now; update_battery();
            /* ★ v1.4.0 修复: handle_charging_state 一直没被调用，
               导致 sess_min/sess_mah/stats_* 恒为 0 ——
               v1.3.1 引入的 charge_counter 精确统计实际从未生效。*/
            handle_charging_state();
            /* ★ v1.5.0: 先算阶段（push_history 会带上），再跑校准状态机 */
            {
                int st = detect_charge_stage();
                if (st != chg_stage) {
                    if (st == 3) log_event("进入恒压(CV)阶段");
                    else if (st == 2 && chg_stage != 2) log_event("进入恒流(CC)阶段");
                    chg_stage = st;
                }
                cal_tick();
                prev_curr = bat_curr;
                prev_volt = bat_volt;
            }
            /* ★ v1.6.0: 系统信息采集（供 App 走模块取数） */
            update_cpu_detail();
            update_cpu_usage();
            update_mem_detail();
            update_partitions();
            update_sys_props();
            update_gpu();
            gpu_temp_c = gpu_temp;   /* 复用 thermal 已采的 GPU 温度给 App */
            update_oem();
            update_device_detail();
            update_screen_and_thermal();
            push_history();
            apply_control(); apply_power_path(); apply_cv_and_current(); apply_native_night(); apply_night_mode(); high_temp_alert();
        }
    }
    log_event("ChargeControl 守护进程停止");
    close(srv); return 0;
}
