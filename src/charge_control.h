#ifndef CHARGE_CONTROL_H
#define CHARGE_CONTROL_H

#include "cjson/cJSON.h"

typedef struct {
    int   capacity;      /* -1 if unavailable */
    char  status[64];
    char  health[64];
    float temperature;   /* -1.0f if unavailable */
    float voltage_mv;    /* -1.0f if unavailable */
    float current_ma;    /* -1.0f if unavailable */
    int   charging_enabled;
    char  timestamp[32];
} battery_status_t;

/* Read current battery status from sysfs. Returns 0 on success, -1 on error. */
int cc_get_battery_status(battery_status_t *out);

/* Enable (1) or disable (0) charging. Returns 0 on success, -1 on failure. */
int cc_set_charging_enabled(int enabled);

/* Set charge limit percent (0-100). Returns 0 on success, -1 on failure. */
int cc_set_charge_limit(int limit_percent);

/* Set charging mode ("normal","fast","trickle","power_saving","super_saver").
 * Returns 0 on success, -1 on failure. */
int cc_set_charging_mode(const char *mode, cJSON *cfg);

/* Temperature protection check.
 * out_temp: current temperature, out_action: "none","charging_stopped","throttled_to_trickle","charging_resumed"
 * Returns 0 on success. */
int cc_check_temperature_protection(cJSON *cfg, float *out_temp, const char **out_action);

#endif /* CHARGE_CONTROL_H */
