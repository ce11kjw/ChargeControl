#include "charge_control.h"
#include "config.h"
#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>

/* Threshold above which a sysfs voltage/current value is treated as μV/μA
   rather than mV/mA.  100 V (100,000 mV) is a realistic upper bound for
   battery millivolt readings, so any value above this must be in μV. */
#define MICROVOLTS_THRESHOLD 100000L
#define MICROAMPS_THRESHOLD  100000L

/* ── module state ────────────────────────────────────────── */

static char s_moddir[512]      = "";
static char s_config_path[600] = "";
static char s_db_path[600]     = "";

volatile int g_temp_stopped_charging = 0;

/* ── sysfs path tables ───────────────────────────────────── */

#define MAX_PATHS 4

static const char *PATHS_CAPACITY[]       = {
    "/sys/class/power_supply/battery/capacity",
    "/sys/class/power_supply/BAT0/capacity",
    NULL
};
static const char *PATHS_STATUS[]         = {
    "/sys/class/power_supply/battery/status",
    "/sys/class/power_supply/BAT0/status",
    NULL
};
static const char *PATHS_TEMP[]           = {
    "/sys/class/power_supply/battery/temp",
    "/sys/class/power_supply/BAT0/temp",
    NULL
};
static const char *PATHS_VOLTAGE[]        = {
    "/sys/class/power_supply/battery/voltage_now",
    "/sys/class/power_supply/BAT0/voltage_now",
    NULL
};
static const char *PATHS_CURRENT[]        = {
    "/sys/class/power_supply/battery/current_now",
    "/sys/class/power_supply/BAT0/current_now",
    NULL
};
static const char *PATHS_HEALTH[]         = {
    "/sys/class/power_supply/battery/health",
    "/sys/class/power_supply/BAT0/health",
    NULL
};
static const char *PATHS_CHARGING_EN[]    = {
    "/sys/class/power_supply/battery/charging_enabled",
    "/sys/kernel/debug/charger/charging_enable",
    "/proc/driver/mmi_battery/charging",
    NULL
};
static const char *PATHS_CHARGE_LIMIT[]   = {
    "/sys/class/power_supply/battery/charge_control_limit",
    "/sys/devices/platform/soc/soc:qti_battery_charger/charge_limit",
    NULL
};
static const char *PATHS_INPUT_CURRENT[]  = {
    "/sys/class/power_supply/battery/input_current_limit",
    "/sys/class/power_supply/usb/input_current_limit",
    NULL
};
static const char *PATHS_CC_CURRENT[]     = {
    "/sys/class/power_supply/battery/constant_charge_current",
    "/sys/class/power_supply/battery/constant_charge_current_max",
    NULL
};

/* ── sysfs helpers ───────────────────────────────────────── */

static int read_sysfs(const char **paths, char *buf, size_t bufsz)
{
    for (int i = 0; paths[i]; i++) {
        FILE *fp = fopen(paths[i], "r");
        if (!fp) continue;
        size_t n = fread(buf, 1, bufsz - 1, fp);
        fclose(fp);
        if (n == 0) continue;
        buf[n] = '\0';
        /* trim trailing whitespace */
        while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r' ||
                         buf[n-1] == ' '  || buf[n-1] == '\t'))
            buf[--n] = '\0';
        return 0;
    }
    return -1;
}

static int write_sysfs(const char **paths, const char *value)
{
    for (int i = 0; paths[i]; i++) {
        FILE *fp = fopen(paths[i], "w");
        if (!fp) continue;
        fputs(value, fp);
        fclose(fp);
        return 0;
    }
    return -1;
}

/* ── mock values (dev / test environments) ───────────────── */

static int mock_capacity(void)
{
    /* Deterministic: use seconds mod 51 so it varies without random() */
    return 40 + (int)(time(NULL) % 51);
}

static double mock_temperature(void)
{
    return 25.0 + (double)(time(NULL) % 16);
}

/* ── public API ──────────────────────────────────────────── */

void cc_init(const char *moddir)
{
    snprintf(s_moddir,      sizeof(s_moddir),      "%s", moddir);
    snprintf(s_config_path, sizeof(s_config_path), "%s/config.json",      moddir);
    snprintf(s_db_path,     sizeof(s_db_path),     "%s/chargecontrol.db", moddir);
}

const char *cc_config_path(void) { return s_config_path; }
const char *cc_db_path(void)     { return s_db_path;     }

int cc_load_config(ChargeConfig *cfg)
{
    return config_load(s_config_path, cfg);
}

int cc_save_config(const ChargeConfig *cfg)
{
    return config_save(s_config_path, cfg);
}

static int is_charging_enabled(void)
{
    char buf[32];
    if (read_sysfs(PATHS_CHARGING_EN, buf, sizeof(buf)) != 0)
        return 1; /* unknown → assume enabled */
    return !(strcmp(buf, "0") == 0 ||
             strcmp(buf, "false") == 0 ||
             strcmp(buf, "disabled") == 0);
}

BatteryStatus cc_get_battery_status(void)
{
    BatteryStatus bs;
    memset(&bs, 0, sizeof(bs));
    bs.voltage_mv = -1;
    bs.current_ma = -1;

    /* capacity */
    char buf[64];
    if (read_sysfs(PATHS_CAPACITY, buf, sizeof(buf)) == 0)
        bs.capacity = atoi(buf);
    else
        bs.capacity = mock_capacity();

    /* status */
    if (read_sysfs(PATHS_STATUS, buf, sizeof(buf)) == 0)
        snprintf(bs.status, sizeof(bs.status), "%s", buf);
    else
        snprintf(bs.status, sizeof(bs.status), "Unknown");

    /* health */
    if (read_sysfs(PATHS_HEALTH, buf, sizeof(buf)) == 0)
        snprintf(bs.health, sizeof(bs.health), "%s", buf);
    else
        snprintf(bs.health, sizeof(bs.health), "Unknown");

    /* temperature: Android reports in tenths of a degree */
    if (read_sysfs(PATHS_TEMP, buf, sizeof(buf)) == 0) {
        long val = atol(buf);
        bs.temperature = (labs(val) > 100) ? val / 10.0 : (double)val;
    } else {
        bs.temperature = mock_temperature();
    }

    /* voltage: Bug fix – threshold changed from 10000 to MICROVOLTS_THRESHOLD */
    if (read_sysfs(PATHS_VOLTAGE, buf, sizeof(buf)) == 0) {
        long uv = atol(buf);
        bs.voltage_mv = (uv > MICROVOLTS_THRESHOLD) ? uv / 1000.0 : (double)uv;
    }

    /* current: Bug fix – same threshold correction */
    if (read_sysfs(PATHS_CURRENT, buf, sizeof(buf)) == 0) {
        long ua = atol(buf);
        bs.current_ma = (labs(ua) > MICROAMPS_THRESHOLD) ? ua / 1000.0 : (double)ua;
    }

    bs.charging_enabled = is_charging_enabled();

    /* ISO-8601 UTC timestamp */
    time_t now = time(NULL);
    struct tm *utc = gmtime(&now);
    strftime(bs.timestamp, sizeof(bs.timestamp), "%Y-%m-%dT%H:%M:%SZ", utc);

    return bs;
}

int cc_set_charging_enabled(int enabled)
{
    return write_sysfs(PATHS_CHARGING_EN, enabled ? "1" : "0");
}

int cc_set_charge_limit(int limit_percent)
{
    if (limit_percent < 0 || limit_percent > 100)
        return -1;

    ChargeConfig cfg;
    cc_load_config(&cfg);
    cfg.max_limit = limit_percent;
    cc_save_config(&cfg);

    char val[8];
    snprintf(val, sizeof(val), "%d", limit_percent);
    write_sysfs(PATHS_CHARGE_LIMIT, val);
    return 0;
}

int cc_set_charging_mode(const char *mode)
{
    ChargeConfig cfg;
    cc_load_config(&cfg);

    ModeConfig *mc = NULL;
    if      (strcmp(mode, "normal")       == 0) mc = &cfg.mode_normal;
    else if (strcmp(mode, "fast")         == 0) mc = &cfg.mode_fast;
    else if (strcmp(mode, "trickle")      == 0) mc = &cfg.mode_trickle;
    else if (strcmp(mode, "power_saving") == 0) mc = &cfg.mode_power_saving;
    else if (strcmp(mode, "super_saver")  == 0) mc = &cfg.mode_super_saver;
    else return -1;

    /* Write current limits to sysfs (value in μA = mA * 1000) */
    char val[32];
    snprintf(val, sizeof(val), "%d", mc->max_current_ma * 1000);
    write_sysfs(PATHS_INPUT_CURRENT, val);
    write_sysfs(PATHS_CC_CURRENT,    val);

    snprintf(cfg.mode, sizeof(cfg.mode), "%s", mode);
    cc_save_config(&cfg);
    return 0;
}

TempProtectionResult cc_check_temperature_protection(void)
{
    ChargeConfig cfg;
    cc_load_config(&cfg);

    BatteryStatus bs = cc_get_battery_status();
    double temp = bs.temperature;

    TempProtectionResult res;
    res.temperature = temp;
    res.threshold   = cfg.temperature_threshold;
    res.critical    = cfg.temperature_critical;
    snprintf(res.action, sizeof(res.action), "none");

    if (temp >= cfg.temperature_critical) {
        cc_set_charging_enabled(0);
        g_temp_stopped_charging = 1;
        snprintf(res.action, sizeof(res.action), "charging_stopped");
    } else if (temp >= cfg.temperature_threshold) {
        cc_set_charging_mode("trickle");
        snprintf(res.action, sizeof(res.action), "throttled_to_trickle");
    } else if (g_temp_stopped_charging) {
        /* Temperature is back within safe range and charging was stopped by us
           (not by the user). Re-enable charging only if it is still disabled —
           the user may have manually re-enabled it already. */
        if (!is_charging_enabled()) {
            cc_set_charging_enabled(1);
            snprintf(res.action, sizeof(res.action), "charging_resumed");
        }
        g_temp_stopped_charging = 0;
    }

    return res;
}

char *cc_battery_status_to_json(const BatteryStatus *bs)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    cJSON_AddNumberToObject(root, "capacity",         bs->capacity);
    cJSON_AddStringToObject(root, "status",           bs->status);
    cJSON_AddStringToObject(root, "health",           bs->health);
    cJSON_AddNumberToObject(root, "temperature",      bs->temperature);
    if (bs->voltage_mv >= 0)
        cJSON_AddNumberToObject(root, "voltage_mv",   bs->voltage_mv);
    else
        cJSON_AddNullToObject(root, "voltage_mv");
    /* -1 is the sentinel value meaning "unavailable" */
    if (bs->current_ma != -1)
        cJSON_AddNumberToObject(root, "current_ma",   bs->current_ma);
    else
        cJSON_AddNullToObject(root, "current_ma");
    cJSON_AddBoolToObject  (root, "charging_enabled", bs->charging_enabled);
    cJSON_AddStringToObject(root, "timestamp",        bs->timestamp);

    char *s = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return s;
}

char *cc_get_all_settings_json(void)
{
    ChargeConfig cfg;
    cc_load_config(&cfg);
    BatteryStatus bs = cc_get_battery_status();

    /* Re-serialise config back to a cJSON tree using config_save logic */
    char tmp_path[] = "/tmp/cc_tmp_XXXXXX";
    int fd = mkstemp(tmp_path);
    if (fd < 0) return NULL;
    close(fd);

    config_save(tmp_path, &cfg);

    FILE *fp = fopen(tmp_path, "r");
    char *cfg_text = NULL;
    if (fp) {
        fseek(fp, 0, SEEK_END);
        long sz = ftell(fp); rewind(fp);
        if (sz > 0) {
            cfg_text = malloc((size_t)sz + 1);
            if (cfg_text) {
                size_t rd = fread(cfg_text, 1, (size_t)sz, fp);
                cfg_text[rd] = '\0';
            }
        }
        fclose(fp);
    }
    unlink(tmp_path);

    cJSON *cfg_obj = cfg_text ? cJSON_Parse(cfg_text) : cJSON_CreateObject();
    free(cfg_text);

    char *bat_json = cc_battery_status_to_json(&bs);
    cJSON *bat_obj = bat_json ? cJSON_Parse(bat_json) : cJSON_CreateObject();
    free(bat_json);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "config",  cfg_obj);
    cJSON_AddItemToObject(root, "battery", bat_obj);

    char *s = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return s;
}
