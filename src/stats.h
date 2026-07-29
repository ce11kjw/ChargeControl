#ifndef STATS_H
#define STATS_H

#include <pthread.h>

/* Initialise the SQLite database (create tables if not present).
   Must be called once before any other stats function.
   Returns 0 on success, -1 on error. */
int stats_init_db(const char *db_path);

/* Record a battery snapshot. Returns the new row id, or -1 on error. */
long long stats_record_snapshot(int capacity, double temperature,
                                double voltage_mv, double current_ma,
                                const char *status, const char *mode);

/* Start a new charging session. Returns session_id, or -1 on error. */
long long stats_start_session(int start_level, const char *mode);

/* End a charging session. Returns 0 on success, -1 on error. */
int stats_end_session(long long session_id, int end_level, double max_temp);

/* Return JSON string for the given period (caller must free()).
   Returns NULL on error. */
char *stats_get_daily_stats(int days);
char *stats_get_weekly_stats(void);
char *stats_get_monthly_stats(void);

/* Return JSON array of the most recent `limit` snapshots (caller free()). */
char *stats_get_recent_snapshots(int limit);

/* Return JSON health summary (caller free()). */
char *stats_get_battery_health(void);

/* Return CSV string of all charging_sessions (caller free()). */
char *stats_export_csv(void);

/* Return JSON array of all charging_sessions (caller free()). */
char *stats_export_json(void);

/* Delete records older than retention_days.  Returns deleted row count. */
int stats_prune_old_data(int retention_days);

/* Mutex for thread-safe DB access (used by snapshot_daemon). */
extern pthread_mutex_t g_db_mutex;

#endif /* STATS_H */
