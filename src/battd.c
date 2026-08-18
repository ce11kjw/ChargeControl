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
#define VERSION "v1.2.58"

static volatile int running = 1;
static int charge_limit = 80;
static int temp_limit = 45;
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
/* 充电统计 */
static time_t sess_start = 0;
static int sess_start_cap = -1;
static int sess_active = 0;
static int sess_min = 0, sess_mah = 0;
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
static int storage_total = -1, storage_free = -1;
static int uptime_secs = -1, cpu_cores = -1;
static char kernel_ver[128] = "", cpu_model[128] = "", device_model[128] = "";
static int wifi_rssi = -128, wifi_on = 0, mobile_on = 0;
static char ip_addr[64] = "";
static int charge_full_mah = 0, capacity_disp = 0, power_mw = 0, est_full_min = 0, est_cycles_left = 0;
static char est_full_txt[32] = "";
static float health_degrade = 0.0f;
static char health_rating[16] = "";
static int bat_capacity = -1, bat_temp = -1, bat_volt = -1, bat_curr = -1;
static char bat_status[32] = "Unknown";
static int soc_temp = -1, gpu_temp = -1, chg_temp = -1, case_temp = -1;
static int cycle_count = -1, health = -1;
static int usb_online = 0, usb_power_max = 0, pd_type = 0;
static int usb_curr_cache = 0, usb_volt_cache = 0;
static char proto_name[32] = "未知";
static char control_state[24] = "idle";

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
    if (fd < 0) return -1; int n = read(fd, buf, sizeof(buf)-1);
    close(fd); if (n <= 0) return -1; buf[n] = '\0'; return atoi(buf);
}
static int read_str(const char *path, char *out, int size) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1; int n = read(fd, out, size-1);
    close(fd); if (n <= 0) return -1; out[n] = '\0';
    while (n > 0 && (out[n-1]=='\n'||out[n-1]=='\r')) out[--n] = '\0';
    return n;
}
static int write_str(const char *path, const char *val) {
    int fd = open(path, O_WRONLY); if (fd < 0) return -1;
    int n = write(fd, val, strlen(val)); close(fd); return n;
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
        FILE *cif = fopen("/proc/cpuinfo", "r");
        if (cif) { char cl[256]; while (fgets(cl, sizeof(cl), cif)) {
            char *p = strstr(cl, "Hardware"); if (p) { char *c = strchr(p, ':'); if (c) { c++; while(*c==' ') c++; snprintf(cpu_model, sizeof(cpu_model), "%s", c); break; } }
        } fclose(cif); }
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

static void update_temps(void) {
    time_t tn = time(NULL);
    if (tn - temps_last < 5) return;  /* 5s 节流 */
    temps_last = tn;
    soc_temp = -1; gpu_temp = -1; chg_temp = -1; case_temp = -1;
    for (int i = 0; i < 50; i++) {
        char tzpath[128]; snprintf(tzpath, sizeof(tzpath), "%s/thermal_zone%d/type", THERMAL, i);
        char tname[64];
        if (read_str(tzpath, tname, sizeof(tname)) <= 0) continue;
        snprintf(tzpath, sizeof(tzpath), "%s/thermal_zone%d/temp", THERMAL, i);
        int tval = read_int(tzpath);
        if (tval < 0) continue;
        if (soc_temp < 0 && (strstr(tname, "soc_max") || strstr(tname, "tsens") || strstr(tname, "cpu-therm") || strstr(tname, "cpu_max") || strstr(tname, "cpuss") || strstr(tname, "cpu_therm") || strstr(tname, "soc_therm")))
            soc_temp = tval / 1000;
        else if (gpu_temp < 0 && (strstr(tname, "gpu1") || strstr(tname, "gpu-therm") || strstr(tname, "gpu_therm") || strstr(tname, "gpu-max") || strstr(tname, "gpu_max") || strstr(tname, "gpu-thermal") || strstr(tname, "gpu_thermal")))
            gpu_temp = tval / 1000;
        else if (chg_temp < 0 && (strstr(tname, "mtk-master-charger") || strstr(tname, "usb-therm") || strstr(tname, "bq") || strstr(tname, "smb") || strstr(tname, "charger-thermal") || strstr(tname, "charger_thermal")))
            chg_temp = tval / 1000;
        else if (case_temp < 0 && (strstr(tname, "X7_therm") || strstr(tname, "charger2") || strstr(tname, "case") || strstr(tname, "skin") || strstr(tname, "quiet_therm") || strstr(tname, "shell") || strstr(tname, "frame") || strstr(tname, "back_therm")))
            case_temp = tval / 1000;
    }
}

static void handle_charging_state(void);
static void update_battery(void) {
    char path[MAX_LINE]; int v;
    snprintf(path, sizeof(path), "%s/capacity", SYSFS);
    if ((v = read_int(path)) >= 0) bat_capacity = v;
    snprintf(path, sizeof(path), "%s/temp", SYSFS);
    if ((v = read_int(path)) >= 0) bat_temp = v / 10;
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
    /* 健康度 */
    if (cycle_count > 0 && charge_full_mah > 0) {
        float loss = (5380.0f - charge_full_mah) / cycle_count;
        if (loss > 0.001f) {
            int est = (int)((charge_full_mah * 0.6f - 5380.0f * 0.6f) / loss);
            if (est < 0) est = 0;
            est_cycles_left = est;
        }
        float health_f = (float)charge_full_mah / 5380.0f * 100.0f;
        if (health_f > 100) health_f = 100;
        if (health_f < 0) health_f = 0;
        health = (int)(health_f * 10 + 0.5f) / 10.0f;
        if (health >= 95) strcpy(health_rating, "优秀");
        else if (health >= 85) strcpy(health_rating, "良好");
        else if (health >= 70) strcpy(health_rating, "一般");
        else if (health >= 60) strcpy(health_rating, "较差");
        else strcpy(health_rating, "需更换");
    }
    slow_update_battery();
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
        if (!strcmp(k, "charge_limit")) charge_limit = atoi(v);
        else if (!strcmp(k, "temp_limit")) temp_limit = atoi(v);
        else if (!strcmp(k, "resume_delta")) resume_delta = atoi(v);
        else if (!strcmp(k, "interval")) interval = atoi(v);
        else if (!strcmp(k, "enabled")) limit_enabled = atoi(v);
        else if (!strcmp(k, "history_enabled")) history_enabled = atoi(v);
    }
    fclose(f);
}
static void save_config(void) {
    FILE *f = fopen(CONFIG, "w"); if (!f) return;
    fprintf(f, "charge_limit=%d\n", charge_limit);
    fprintf(f, "temp_limit=%d\n", temp_limit);
    fprintf(f, "resume_delta=%d\n", resume_delta);
    fprintf(f, "interval=%d\n", interval);
    fprintf(f, "enabled=%d\n", limit_enabled);
    fprintf(f, "history_enabled=%d\n", history_enabled);
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
}
static void save_extras(void) {
    FILE *f = fopen("/data/adb/battery-manager/extras.json", "w");
    if (!f) return;
    fprintf(f, "{\"night\":%d,\"ns\":%d,\"ne\":%d,\"scene\":%d,\"webhook\":\"%s\",\"lang\":%d,\"theme\":%d}",
        night_enabled, night_start_h, night_end_h, scene, webhook_url, lang, theme);
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
    } else if (charging) {
        sess_min = (int)((time(NULL) - sess_start) / 60);
    }
    if (!charging && sess_active) {
        sess_active = 0;
        if (sess_min > 0 || sess_mah > 0) {
            stats_count++; stats_min += sess_min; stats_mah += sess_mah;
            save_stats();
        }
    }
    if (sess_active && sess_start_cap >= 0 && bat_capacity > sess_start_cap) {
        int cf = read_int(SYSFS "/charge_full");
        int chg_full = cf > 0 ? cf / 1000 : 4000;
        sess_mah = ((bat_capacity - sess_start_cap) * chg_full) / 100;
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
                snprintf(cmd, sizeof(cmd), "curl -s -m 5 -X POST -d 'temp=%d' '%s' >/dev/null 2>&1", bat_temp, webhook_url);
                FILE *wh = popen(cmd, "r");
                if (wh) pclose(wh);
            } else {
                log_event("webhook URL invalid chars");
            }
        }
    }
    if (bat_temp < 45) alerted_high = 0;
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
    fprintf(f, "{\"t\":%ld,\"c\":%d,\"tmp\":%d,\"v\":%d,\"s\":\"%s\"}\n",
            (long)now, bat_capacity, bat_temp, bat_volt, bat_status);
    fclose(f);
    last_save = now;
    if (sess_active && now - session_30s >= 30) {
        session_30s = now;
        FILE *sf = fopen("/data/adb/battery-manager/session.jsonl", "a");
        if (sf) {
            fprintf(sf, "{\"t\":%ld,\"c\":%d,\"mah\":%d,\"tmp\":%d}\n",
                (long)now, bat_capacity, sess_mah, bat_temp);
            fclose(sf);
        }
    }
    if (!sess_active) session_30s = 0;
}

static char *history_json(void) {
    FILE *f = fopen(HISTORY, "r"); if (!f) return strdup("[]");
    fseek(f, 0, SEEK_END); long sz = ftell(f); rewind(f);
    if (sz <= 0 || sz > (1<<20)) { fclose(f); return strdup("[]"); }
    char *buf = malloc(sz + 1); size_t rd = fread(buf, 1, sz, f); fclose(f);
    buf[rd] = '\0';
    int nl = 0; for (char *s = buf; *s; s++) if (*s == '\n') nl++;
    int keep = nl > HIST_MAX ? nl - HIST_MAX : 0;
    char *start = buf; int seen = 0;
    for (char *s = buf; *s && seen < keep; s++) if (*s == '\n') { start = s+1; seen++; }
    for (char *p = start; *p; p++) if (*p == '\n') *p = ',';
    size_t blen = strlen(start);
    if (blen > 0 && start[blen-1] == ',') start[blen-1] = '\0', blen--;
    char *out = malloc(blen + 3); out[0] = '[';
    memcpy(out+1, start, blen); out[blen+1] = ']'; out[blen+2] = '\0';
    free(buf); return out;
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
    char body[8192];
    update_temps();
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
    int bypass_ok = bypass_supported, bypass_on = 0;
    if (bypass_ok) {
        int bv = read_int(bypass_node);
        if (bv > 0) bypass_on = 1;
    }
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
    snprintf(body, sizeof(body),
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
        "\"mem_total\":%d,\"mem_avail\":%d,\"mem_free\":%d,\"storage_total\":%d,\"storage_free\":%d,\"uptime_secs\":%d,\"cpu_cores\":%d,\"kernel_ver\":\"%s\",\"cpu_model\":\"%s\",\"device_model\":\"%s\",\"wifi_rssi\":%d,\"wifi_on\":%d,\"mobile_on\":%d,\"ip_addr\":\"%s\",\"bat_technology\":\"%s\",\"bat_capacity_level\":\"%s\",\"bat_health\":\"%s\",\"charge_type\":%d,\"time_to_full\":%d,\"charge_counter\":%d,\"input_current_limit\":%d,\"bat_manufacturer\":\"%s\",\"bat_model_name\":\"%s\",\"voltage_ocv\":%d,\"current_avg\":%d,\"temp_ambient\":%d,\"constant_charge_current\":%d,\"constant_charge_voltage\":%d,\"capacity_error_margin\":%d,\"time_to_empty\":%d,\"bat_present\":%d,\"internal_resistance\":%d,\"charge_now\":%d,\"charge_term_current\":%d,\"constant_charge_current_max\":%d,\"constant_charge_voltage_max\":%d,\"temp_max\":%d,\"temp_min\":%d,\"charger_temp\":%d,\"charge_done\":%d,\"input_voltage_settled\":%d,\"safety_timer_expired\":%d,\"calibrate\":%d,\"webhook\":\"%s\",\"bypass_ok\":%d,\"bypass_on\":%d,\"sess_active\":%d,\"sess_min\":%d,\"sess_mah\":%d,"
        "\"stats_count\":%d,\"stats_min\":%ld,\"stats_mah\":%ld,\"night\":%d,\"scene\":%d,\"lang\":%d,\"theme\":%d,\"alerted\":%d}",
        bat_capacity, capacity_disp, bat_temp, vol_mv, cur_ma, bat_status,
        limit_enabled, charge_limit, temp_limit, resume_delta, interval,
        soc_temp, gpu_temp, chg_temp, case_temp,
        cycle_count, health, health_rating,
        charge_full, est_cycles_left,
        power_mw, est_full_min, est_full_txt, health_degrade,
        usb_online, proto_name, pd_type, usb_power_max,
        control_state, full_once, (long)full_until, history_enabled, hw_paused, proc_name_buf, proc_pid_val, cpu_pct,
        mem_total, mem_avail, mem_free, storage_total, storage_free, uptime_secs, cpu_cores, kernel_ver, cpu_model, device_model, wifi_rssi, wifi_on, mobile_on, ip_addr, bat_technology, bat_capacity_level, bat_health, bat_charge_type, bat_time_to_full, bat_charge_counter, bat_input_current_limit, bat_manufacturer, bat_model_name, voltage_ocv, current_avg, temp_ambient, constant_charge_current, constant_charge_voltage, capacity_error_margin, time_to_empty, bat_present, internal_resistance, charge_now, charge_term_current, constant_charge_current_max, constant_charge_voltage_max, temp_max, temp_min, charger_temp, charge_done, input_voltage_settled, safety_timer_expired, calibrate, webhook_url, bypass_ok, bypass_on, sess_active, sess_min, sess_mah, stats_count, stats_min, stats_mah, night_enabled, scene, lang, theme, alerted_high);
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
}

static void handle_limit(int fd, const char *body) {
    if (body) {
        char *copy = strdup(body); char *save = NULL;
        for (char *tok = strtok_r(copy, "&", &save); tok; tok = strtok_r(NULL, "&", &save)) {
            char *eq = strchr(tok, '='); if (!eq) continue;
            *eq = '\0'; char key[64], val[512];
            strncpy(key, tok, 63); key[63] = '\0';
            strncpy(val, eq+1, 511); val[511] = '\0';
            const char *dec = val;
            if (!strcmp(key, "charge_limit")) { int x = atoi(dec); if (x >= 20 && x <= 100) charge_limit = x; }
            else if (!strcmp(key, "temp_limit")) { int x = atoi(dec); if (x >= 30 && x <= 60) temp_limit = x; }
            else if (!strcmp(key, "resume_delta")) { int x = atoi(dec); if (x >= 1 && x <= 20) resume_delta = x; }
            else if (!strcmp(key, "interval")) { int x = atoi(dec); if (x >= 1 && x <= 60) interval = x; }
            else if (!strcmp(key, "enabled")) limit_enabled = atoi(dec);
            else if (!strcmp(key, "history_enabled")) history_enabled = atoi(dec);
            else if (!strcmp(key, "bypass") && bypass_supported) write_str(bypass_node, atoi(dec) ? "1" : "0");
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
        save_config(); apply_control(); apply_night_mode(); high_temp_alert();
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
    FILE *cf = popen("curl -s --max-time 8 https://api.github.com/repos/ce11kjw/ChargeControl/releases/latest", "r");
    if (!cf) { send_resp(fd, 200, "application/json", "{\"update_available\":false,\"error\":\"network\"}"); return; }
    char cbuf[2048]; size_t cl = fread(cbuf, 1, sizeof(cbuf)-1, cf); pclose(cf);
    cbuf[cl] = '\0';
    char *tag = strstr(cbuf, "\"tag_name\":\"");
    if (!tag) { send_resp(fd, 200, "application/json", "{\"update_available\":false,\"error\":\"parse\"}"); return; }
    tag += 12; char latest[32]; int i = 0;
    while (*tag && *tag != '"' && i < 31) latest[i++] = *tag++;
    latest[i] = '\0';
    int newer = ver_cmp(latest, VERSION);
    char resp[256];
    snprintf(resp, sizeof(resp), "{\"update_available\":%s,\"latest\":\"%s\",\"current\":\"%s\"}", newer > 0 ? "true" : "false", latest, VERSION);
    send_resp(fd, 200, "application/json", resp);
}

static void handle_update_apply(int fd) {
    /* 获取最新 release 下载 URL */
    FILE *cf = popen("curl -s --max-time 8 https://api.github.com/repos/ce11kjw/ChargeControl/releases/latest", "r");
    if (!cf) { send_resp(fd, 200, "application/json", "{\"ok\":false,\"msg\":\"network\"}"); return; }
    char cbuf[4096]; size_t cl = fread(cbuf, 1, sizeof(cbuf)-1, cf); pclose(cf);
    cbuf[cl] = '\0';
    char *url = strstr(cbuf, "\"browser_download_url\":\"");
    if (!url) { send_resp(fd, 200, "application/json", "{\"ok\":false,\"msg\":\"parse\"}"); return; }
    url += 23; char dl_url[512]; int i = 0;
    while (*url && *url != '"' && i < 511) dl_url[i++] = *url++;
    dl_url[i] = '\0';
    /* 下载 zip */
    char cmd[2048];
    int my_pid = (int)getpid();
    const char *wr = WEBROOT;
    snprintf(cmd, sizeof(cmd), "curl -sL --max-time 30 -o /tmp/chargecontrol_update.zip '%s' && "
        "mkdir -p /tmp/chargecontrol_update && "
        "unzip -qo /tmp/chargecontrol_update.zip -d /tmp/chargecontrol_update && "
        "cp -f /tmp/chargecontrol_update/webroot/index.html %s/index.html && "
        "cp -f /tmp/chargecontrol_update/bin/battd /data/adb/battery-manager/battd.new && "
        "cp -f /tmp/chargecontrol_update/module.prop /data/adb/battery-manager/module.prop && "
        "chmod 755 /data/adb/battery-manager/battd.new && "
        "rm -rf /tmp/chargecontrol_update /tmp/chargecontrol_update.zip && "
        "kill -TERM %d 2>/dev/null", dl_url, wr, my_pid);
    FILE *pf = popen(cmd, "r");
    if (!pf) { send_resp(fd, 200, "application/json", "{\"ok\":false,\"msg\":\"exec\"}"); return; }
    char res[256]; size_t rn = fread(res, 1, sizeof(res)-1, pf);
    int st = pclose(pf);
    (void)res; (void)rn;
    if (st == 0) {
        log_event("OTA 更新成功，daemon 重启中");
        send_resp(fd, 200, "application/json", "{\"ok\":true,\"msg\":\"更新完成，服务重启中\"}");
    } else {
        send_resp(fd, 200, "application/json", "{\"ok\":false,\"msg\":\"download_fail\"}");
    }
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
    char buf[MAX_LINE]; int n = read(fd, buf, sizeof(buf)-1);
    if (n <= 0) return; buf[n] = '\0';
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
    if (!strcmp(uri, "/api/update-apply")) { handle_update_apply(fd); return; }
    if (!strcmp(uri, "/api/history")) { char *j = history_json(); send_resp(fd, 200, "application/json", j); free(j); return; }
    if (!strcmp(method, "GET")) serve_file(fd, uri);
    else send_resp(fd, 405, "text/plain", "Method Not Allowed");
}

static void on_sig(int sig) { (void)sig; running = 0; }

int main(void) {
    load_config(); load_stats(); load_extras(); last_save = time(NULL);
    update_battery();
    /* 探测旁路支持 */
    FILE *bf = popen("ls /sys/class/power_supply/*/bypass_charger /sys/class/power_supply/*/charge_bypass 2>/dev/null | head -1", "r");
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
            last_poll = now; update_battery(); push_history();
            apply_control(); apply_night_mode(); high_temp_alert();
        }
    }
    log_event("ChargeControl 守护进程停止");
    close(srv); return 0;
}
