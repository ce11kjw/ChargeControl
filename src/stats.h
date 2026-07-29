/* stats.h - 简化版 */
#ifndef STATS_H
#define STATS_H

#include <stddef.h>

int st_init_db(void);
int stats_init_db(const char *db_path);
int st_record_snapshot(int capacity, double temperature,
                       double voltage_mv, double current_ma,
                       const char *status, const char *mode);
int stats_record_snapshot(int capacity, double temperature,
                          double voltage_mv, double current_ma,
                          const char *status, const char *mode);
int stats_get_battery_health(char *health, size_t len);
int st_get_battery_health(char *health, size_t len);
char* stats_get_daily_stats(int days);
char* stats_get_weekly_stats(void);
char* stats_get_monthly_stats(void);
char* stats_get_recent_snapshots(int count);
char* stats_export_csv(void);
char* stats_export_json(void);
int stats_prune_old_data(int days);

#endif /* STATS_H */
