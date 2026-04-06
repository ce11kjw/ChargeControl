#ifndef SNAPSHOT_DAEMON_H
#define SNAPSHOT_DAEMON_H

/* Start snapshot daemon in a background thread.
 * db_path:      SQLite database path
 * config_path:  config.json path
 * interval_s:   snapshot interval in seconds (0 → default 30) */
void snapshot_daemon_start(const char *db_path, const char *config_path, int interval_s);

/* Signal the daemon to stop and wait for it to finish. */
void snapshot_daemon_stop(void);

#endif /* SNAPSHOT_DAEMON_H */
