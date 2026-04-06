#include "charge_control.h"
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <pthread.h>

/* ------------------------------------------------------------------ */
/* sysfs path tables (mirrors charge_control.py)                       */
/* ------------------------------------------------------------------ */

static const char *BATTERY_CAPACITY_PATHS[] = {
    "/sys/class/power_supply/battery/capacity",
    "/sys/class/power_supply/BAT0/capacity",
    NULL
};

static const char *BATTERY_STATUS_PATHS[] = {
    "/sys/class/power_supply/battery/status",
    "/sys/class/power_supply/BAT0/status",
    NULL
};

static const char *BATTERY_TEMP_PATHS[] = {
    "/sys/class/power_supply/battery/temp",
    "/sys/class/power_supply/BAT0/temp",
    NULL
};

static const char *BATTERY_VOLTAGE_PATHS[] = {
    "/sys/class/power_supply/battery/voltage_now",
    "/sys/class/power_supply/BAT0/voltage_now",
    NULL
};

static const char *BATTERY_CURRENT_PATHS[] = {
    "/sys/class/power_supply/battery/current_now",
    "/sys/class/power_supply/BAT0/current_now",
    NULL
};

static const char *BATTERY_HEALTH_PATHS[] = {
    "/sys/class/power_supply/battery/health",
    "/sys/class/power_supply/BAT0/health",
    NULL
};

static const char *CHARGING_ENABLED_PATHS[] = {
    "/sys/class/power_supply/battery/charging_enabled",
    "/sys/kernel/debug/charger/charging_enable",
    "/proc/driver/mmi_battery/charging",
    NULL
};

static const char *INPUT_CURRENT_LIMIT_PATHS[] = {
    "/sys/class/power_supply/battery/input_current_limit",
    "/sys/class/power_supply/usb/input_current_limit",
    NULL
};

static const char *CONSTANT_CHARGE_CURRENT_PATHS[] = {
    "/sys/class/power_supply/battery/constant_charge_current",
    "/sys/class/power_supply/battery/constant_charge_current_max",
    NULL
};

static const char *CHARGE_CONTROL_LIMIT_PATHS[] = {
    "/sys/class/power_supply/battery/charge_control_limit",
    "/sys/devices/platform/soc/soc:qti_battery_charger/charge_limit",
    NULL
};

/* Built-in mode defaults (matches charge_control.py) */
typedef struct {
    const char *name;
    int max_current_ma;
    int max_voltage_mv;
} mode_cfg_t;

static const mode_cfg_t BUILTIN_MODES[] = {
    { "normal",       2000, 4350 },
    { "fast",         4000, 4400 },
    { "trickle",       500, 4200 },
    { "power_saving", 1000, 4300 },
    { "super_saver",   300, 4100 },
    { NULL, 0, 0 }
};

/* Mutex for sysfs access */
static pthread_mutex_t sysfs_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ------------------------------------------------------------------ */
/* Internal helpers                                                    */
/* ------------------------------------------------------------------ */

/* Read first readable path from list; buf must be at least buf_len bytes.
 * Returns 0 on success (buf filled with trimmed value), -1 if no path readable. */
static int read_sysfs(const char **paths, char *buf, size_t buf_len)
{
    FILE *f = NULL;
    size_t n = 0;
    int i = 0;

    for (i = 0; paths[i] != NULL; i++) {
        f = fopen(paths[i], "r");
        if (!f) continue;
        n = fread(buf, 1, buf_len - 1, f);
        fclose(f);
        buf[n] = '\0';
        /* strip trailing whitespace */
        while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r' || buf[n-1] == ' ')) {
            buf[--n] = '\0';
        }
        return 0;
    }
    return -1;
}

/* Write value to first writable path. Returns 0 on success, -1 on failure. */
static int write_sysfs(const char **paths, const char *value)
{
    FILE *f = NULL;
    int i = 0;

    for (i = 0; paths[i] != NULL; i++) {
        f = fopen(paths[i], "w");
        if (!f) continue;
        if (fputs(value, f) != EOF) {
            fclose(f);
            return 0;
        }
        fclose(f);
    }
    return -1;
}

static int is_charging_enabled(void)
{
    char buf[32];
    if (read_sysfs(CHARGING_ENABLED_PATHS, buf, sizeof(buf)) != 0) {
        return 1; /* unknown: assume enabled */
    }
    if (strcmp(buf, "0") == 0 || strcmp(buf, "false") == 0 || strcmp(buf, "disabled") == 0) {
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

int cc_get_battery_status(battery_status_t *out)
{
    char buf[64];
    time_t now;
    struct tm tm_utc;

    if (!out) return -1;

    memset(out, 0, sizeof(*out));
    out->capacity    = -1;
    out->temperature = -1.0f;
    out->voltage_mv  = -1.0f;
    out->current_ma  = -1.0f;
    strncpy(out->status, "Unknown", sizeof(out->status) - 1);
    strncpy(out->health, "Unknown", sizeof(out->health) - 1);

    pthread_mutex_lock(&sysfs_mutex);

    /* capacity */
    if (read_sysfs(BATTERY_CAPACITY_PATHS, buf, sizeof(buf)) == 0) {
        out->capacity = atoi(buf);
    }
    /* Bug fix: sysfs unavailable → return -1, never a random value */

    /* status */
    if (read_sysfs(BATTERY_STATUS_PATHS, buf, sizeof(buf)) == 0) {
        snprintf(out->status, sizeof(out->status), "%s", buf);
    }

    /* health */
    if (read_sysfs(BATTERY_HEALTH_PATHS, buf, sizeof(buf)) == 0) {
        snprintf(out->health, sizeof(out->health), "%s", buf);
    }

    /* temperature: Android reports in tenths of degree Celsius */
    if (read_sysfs(BATTERY_TEMP_PATHS, buf, sizeof(buf)) == 0) {
        long val = atol(buf);
        if (val > 100 || val < -100) {
            out->temperature = (float)val / 10.0f;
        } else {
            out->temperature = (float)val;
        }
    }

    /* voltage: microvolts → millivolts */
    if (read_sysfs(BATTERY_VOLTAGE_PATHS, buf, sizeof(buf)) == 0) {
        long uv = atol(buf);
        if (uv > 10000) {
            out->voltage_mv = (float)uv / 1000.0f;
        } else {
            out->voltage_mv = (float)uv;
        }
    }

    /* current: microamperes → milliamperes */
    if (read_sysfs(BATTERY_CURRENT_PATHS, buf, sizeof(buf)) == 0) {
        long ua = atol(buf);
        if (ua > 10000 || ua < -10000) {
            out->current_ma = (float)ua / 1000.0f;
        } else {
            out->current_ma = (float)ua;
        }
    }

    out->charging_enabled = is_charging_enabled();

    pthread_mutex_unlock(&sysfs_mutex);

    /* timestamp (UTC ISO-8601) */
    time(&now);
    gmtime_r(&now, &tm_utc);
    strftime(out->timestamp, sizeof(out->timestamp), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);

    return 0;
}

int cc_set_charging_enabled(int enabled)
{
    int ret;
    const char *value = enabled ? "1" : "0";
    pthread_mutex_lock(&sysfs_mutex);
    ret = write_sysfs(CHARGING_ENABLED_PATHS, value);
    pthread_mutex_unlock(&sysfs_mutex);
    return ret;
}

int cc_set_charge_limit(int limit_percent)
{
    char buf[16];
    int ret;

    if (limit_percent < 0 || limit_percent > 100) return -1;

    snprintf(buf, sizeof(buf), "%d", limit_percent);
    pthread_mutex_lock(&sysfs_mutex);
    ret = write_sysfs(CHARGE_CONTROL_LIMIT_PATHS, buf);
    pthread_mutex_unlock(&sysfs_mutex);
    return ret;
}

int cc_set_charging_mode(const char *mode, cJSON *cfg)
{
    int max_current_ma = 0;
    int i;
    char buf[32];
    int ok1 = 0, ok2 = 0;

    if (!mode) return -1;

    /* Look up in config first, then fall back to builtin table */
    if (cfg) {
        cJSON *modes_obj = cJSON_GetObjectItem(cfg, "modes");
        if (modes_obj) {
            cJSON *mode_obj = cJSON_GetObjectItem(modes_obj, mode);
            if (mode_obj) {
                cJSON *cur = cJSON_GetObjectItem(mode_obj, "max_current_ma");
                if (cJSON_IsNumber(cur)) max_current_ma = cur->valueint;
            }
        }
    }

    if (max_current_ma == 0) {
        for (i = 0; BUILTIN_MODES[i].name != NULL; i++) {
            if (strcmp(BUILTIN_MODES[i].name, mode) == 0) {
                max_current_ma = BUILTIN_MODES[i].max_current_ma;
                break;
            }
        }
    }

    if (max_current_ma == 0) return -1; /* unknown mode */

    snprintf(buf, sizeof(buf), "%d", max_current_ma * 1000);

    pthread_mutex_lock(&sysfs_mutex);
    ok1 = write_sysfs(INPUT_CURRENT_LIMIT_PATHS, buf);
    ok2 = write_sysfs(CONSTANT_CHARGE_CURRENT_PATHS, buf);
    pthread_mutex_unlock(&sysfs_mutex);

    /* Return 0 if at least one write succeeded (mirrors Python behaviour) */
    return (ok1 == 0 || ok2 == 0) ? 0 : -1;
}

int cc_check_temperature_protection(cJSON *cfg, float *out_temp, const char **out_action)
{
    battery_status_t bat;
    float threshold = 40.0f;
    float critical  = 45.0f;
    static const char *action = "none";

    if (cfg) {
        cJSON *charging = cJSON_GetObjectItem(cfg, "charging");
        if (charging) {
            cJSON *thr = cJSON_GetObjectItem(charging, "temperature_threshold");
            cJSON *cri = cJSON_GetObjectItem(charging, "temperature_critical");
            if (cJSON_IsNumber(thr)) threshold = (float)thr->valuedouble;
            if (cJSON_IsNumber(cri)) critical  = (float)cri->valuedouble;
        }
    }

    if (cc_get_battery_status(&bat) != 0) return -1;

    if (out_temp) *out_temp = bat.temperature;

    if (bat.temperature >= critical) {
        cc_set_charging_enabled(0);
        action = "charging_stopped";
    } else if (bat.temperature >= threshold) {
        cc_set_charging_mode("trickle", cfg);
        action = "throttled_to_trickle";
    } else if (!bat.charging_enabled) {
        cc_set_charging_enabled(1);
        action = "charging_resumed";
    } else {
        action = "none";
    }

    if (out_action) *out_action = action;
    return 0;
}
