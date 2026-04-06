#ifndef STATS_H
#define STATS_H

#include <sqlite3.h>

/* Initialise database, creating tables if missing. Returns 0 on success. */
int stats_init_db(const char *db_path);

/* Record a battery snapshot. Returns 0 on success. */
int stats_record_snapshot(int capacity, float temperature, float voltage_mv,
                          float current_ma, const char *status, const char *mode);

/* Begin a charging session. *out_id receives the new row id. Returns 0 on success. */
int stats_start_session(int start_level, const char *mode, sqlite3_int64 *out_id);

/* End a charging session. Returns 0 on success. */
int stats_end_session(sqlite3_int64 session_id, int end_level, float max_temp);

/* Statistics queries – each allocates a JSON string in *out_json (caller must free). */
int stats_get_daily(int days, char **out_json);
int stats_get_weekly(char **out_json);
int stats_get_monthly(char **out_json);
int stats_get_recent_snapshots(int limit, char **out_json);
int stats_get_health_trend(char **out_json);

/* Export – allocate result string in *out (caller must free). */
int stats_export_csv(char **out_csv);
int stats_export_json(char **out_json);

/* Delete records older than retention_days. Returns number of rows deleted. */
int stats_prune_old_data(int retention_days);

/* Close database handle (called on shutdown). */
void stats_close_db(void);

#endif /* STATS_H */
