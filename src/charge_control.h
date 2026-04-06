#ifndef CHARGE_CONTROL_H
#define CHARGE_CONTROL_H

#include "config.h"

/* Battery status snapshot */
typedef struct {
    int    capacity;         /* percentage 0-100 */
    char   status[64];       /* Charging / Discharging / Full / Unknown */
    char   health[64];       /* Good / Overheat / … */
    double temperature;      /* degrees Celsius */
    double voltage_mv;       /* millivolts (or -1 if unavailable) */
    double current_ma;       /* milliamps  (or -1 if unavailable) */
    int    charging_enabled; /* 1 = enabled, 0 = disabled */
    char   timestamp[32];    /* ISO-8601 UTC, e.g. "2024-01-01T00:00:00Z" */
} BatteryStatus;

/* Result of a temperature-protection check */
typedef struct {
    double temperature;
    int    threshold;
    int    critical;
    char   action[32];       /* none / throttled_to_trickle / charging_stopped / charging_resumed */
} TempProtectionResult;

/* Temperature-protection state: set to 1 when charging was stopped due to
   overheating; cleared when temperature drops back to normal.
   The HTTP handlers read/write this flag under the same global config lock. */
extern volatile int g_temp_stopped_charging;

/* Initialise the module (resolves MODDIR, loads config path). */
void cc_init(const char *moddir);

/* Return path to config.json (static buffer). */
const char *cc_config_path(void);

/* Return path to chargecontrol.db (static buffer). */
const char *cc_db_path(void);

/* Read live battery status from sysfs. Falls back to mock values in
   development environments where sysfs is unavailable. */
BatteryStatus cc_get_battery_status(void);

/* Enable (1) or disable (0) charging via sysfs. */
int cc_set_charging_enabled(int enabled);

/* Set the charge limit percentage (0-100).  Updates config.json.
   Returns 0 on success, -1 on invalid value. */
int cc_set_charge_limit(int limit_percent);

/* Apply a named charging mode.  Updates config.json.
   Returns 0 on success, -1 if the mode name is unknown. */
int cc_set_charging_mode(const char *mode);

/* Run temperature-protection logic. */
TempProtectionResult cc_check_temperature_protection(void);

/* Load config into *cfg from config.json. */
int cc_load_config(ChargeConfig *cfg);

/* Save *cfg back to config.json. */
int cc_save_config(const ChargeConfig *cfg);

/* Serialise BatteryStatus to a newly-malloc'd JSON string.
   Caller must free(). */
char *cc_battery_status_to_json(const BatteryStatus *bs);

/* Return a newly-malloc'd JSON string with merged config + battery.
   Caller must free(). */
char *cc_get_all_settings_json(void);

#endif /* CHARGE_CONTROL_H */
