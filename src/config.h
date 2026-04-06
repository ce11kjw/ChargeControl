#ifndef CONFIG_H
#define CONFIG_H

#define MODE_NAME_LEN 32
#define HOST_LEN 64
#define VERSION_LEN 32

typedef struct {
    int max_current_ma;
    int max_voltage_mv;
    char description[128];
} ModeConfig;

typedef struct {
    char version[VERSION_LEN];

    /* charging */
    int  max_limit;
    int  min_limit;
    char mode[MODE_NAME_LEN];
    int  fast_charge_enabled;
    int  temperature_threshold;
    int  temperature_critical;

    /* modes */
    ModeConfig mode_normal;
    ModeConfig mode_fast;
    ModeConfig mode_trickle;
    ModeConfig mode_power_saving;
    ModeConfig mode_super_saver;

    /* server */
    char server_host[HOST_LEN];
    int  server_port;
    int  server_debug;

    /* stats */
    int stats_enabled;
    int stats_retention_days;

    /* notifications */
    int notif_enabled;
    int notif_charge_complete;
    int notif_temperature_warning;
} ChargeConfig;

/* Load config from JSON file. Returns 0 on success, -1 on error.
   On error cfg is populated with built-in defaults. */
int config_load(const char *path, ChargeConfig *cfg);

/* Save config to JSON file. Returns 0 on success, -1 on error. */
int config_save(const char *path, const ChargeConfig *cfg);

/* Fill cfg with built-in defaults. */
void config_defaults(ChargeConfig *cfg);

#endif /* CONFIG_H */
