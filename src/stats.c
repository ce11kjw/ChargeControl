/* stats.c - 简化版 */
#include "stats.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_stats_enabled = 0;

int st_init_db(void) {
    g_stats_enabled = 1;
    return 0;
}

int stats_init_db(const char *db_path) {
    (void)db_path;
    return st_init_db();
}

int st_record_snapshot(int capacity, double temperature,
                       double voltage_mv, double current_ma,
                       const char *status, const char *mode) {
    (void)capacity;
    (void)temperature;
    (void)voltage_mv;
    (void)current_ma;
    (void)status;
    (void)mode;
    return 0;
}

int stats_record_snapshot(int capacity, double temperature,
                          double voltage_mv, double current_ma,
                          const char *status, const char *mode) {
    return st_record_snapshot(capacity, temperature, voltage_mv, current_ma, status, mode);
}

int stats_get_battery_health(char *health, size_t len) {
    if (health && len > 0) {
        snprintf(health, len, "Good");
    }
    return 0;
}

int st_get_battery_health(char *health, size_t len) {
    return stats_get_battery_health(health, len);
}

char* stats_get_daily_stats(int days) {
    (void)days;
    char *json = malloc(256);
    if (json) {
        snprintf(json, 256, "{\"charges\":3,\"duration\":\"4h32m\",\"usage\":45,\"avg_power\":12.3}");
    }
    return json;
}

char* stats_get_weekly_stats(void) {
    char *json = malloc(256);
    if (json) {
        snprintf(json, 256, "{\"charges\":21,\"duration\":\"28h\",\"usage\":315,\"avg_power\":11.8}");
    }
    return json;
}

char* stats_get_monthly_stats(void) {
    char *json = malloc(256);
    if (json) {
        snprintf(json, 256, "{\"charges\":90,\"duration\":\"120h\",\"usage\":1350,\"avg_power\":12.0}");
    }
    return json;
}

char* stats_get_recent_snapshots(int count) {
    (void)count;
    char *json = malloc(512);
    if (json) {
        snprintf(json, 512, "[{\"ts\":1690000000,\"cap\":85,\"temp\":32.5,\"volt\":4200,\"curr\":2500}]");
    }
    return json;
}

char* stats_export_csv(void) {
    char *csv = malloc(1024);
    if (csv) {
        snprintf(csv, 1024, "timestamp,capacity,temperature,voltage,current\n2024-07-29 19:00:00,85,32.5,4200,2500\n");
    }
    return csv;
}

char* stats_export_json(void) {
    return stats_get_daily_stats(7);
}

int stats_prune_old_data(int days) {
    (void)days;
    return 0;
}
