#include "stats.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <pthread.h>

/* ------------------------------------------------------------------ */
/* Global state                                                        */
/* ------------------------------------------------------------------ */

static sqlite3 *g_db = NULL;
static pthread_mutex_t db_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

/* Get current UTC time as ISO-8601 string "YYYY-MM-DDTHH:MM:SS" */
static void utc_now(char *buf, size_t len)
{
    time_t t;
    struct tm tm_utc;
    time(&t);
    gmtime_r(&t, &tm_utc);
    strftime(buf, len, "%Y-%m-%dT%H:%M:%S", &tm_utc);
}

/* Append a formatted string to a heap buffer, growing as needed.
 * *buf may be NULL (will be allocated). *cap / *len track capacity and used length. */
static int buf_append(char **buf, size_t *cap, size_t *len, const char *fmt, ...)
{
    int n = 0;
    va_list ap, ap2;

    va_start(ap, fmt);
    va_copy(ap2, ap);
    n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);

    if (n < 0) { va_end(ap2); return -1; }

    if (*len + (size_t)n + 1 > *cap) {
        size_t new_cap = (*cap == 0) ? 1024 : *cap * 2;
        while (new_cap < *len + (size_t)n + 1) new_cap *= 2;
        char *tmp = (char *)realloc(*buf, new_cap);
        if (!tmp) { va_end(ap2); return -1; }
        *buf = tmp;
        *cap = new_cap;
    }

    vsnprintf(*buf + *len, *cap - *len, fmt, ap2);
    va_end(ap2);
    *len += (size_t)n;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Database init                                                       */
/* ------------------------------------------------------------------ */

int stats_init_db(const char *db_path)
{
    int rc;
    char *err = NULL;
    static const char *schema =
        "CREATE TABLE IF NOT EXISTS charging_sessions ("
        "  id          INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  start_time  TEXT NOT NULL,"
        "  end_time    TEXT,"
        "  start_level INTEGER NOT NULL,"
        "  end_level   INTEGER,"
        "  mode        TEXT NOT NULL DEFAULT 'normal',"
        "  max_temp    REAL,"
        "  duration_s  INTEGER,"
        "  efficiency  REAL"
        ");"
        "CREATE TABLE IF NOT EXISTS battery_snapshots ("
        "  id          INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  timestamp   TEXT NOT NULL,"
        "  capacity    INTEGER,"
        "  temperature REAL,"
        "  voltage_mv  REAL,"
        "  current_ma  REAL,"
        "  status      TEXT,"
        "  mode        TEXT"
        ");";

    if (!db_path) return -1;

    pthread_mutex_lock(&db_mutex);

    if (g_db) {
        pthread_mutex_unlock(&db_mutex);
        return 0; /* already open */
    }

    rc = sqlite3_open(db_path, &g_db);
    if (rc != SQLITE_OK) {
        g_db = NULL;
        pthread_mutex_unlock(&db_mutex);
        return -1;
    }

    rc = sqlite3_exec(g_db, schema, NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        sqlite3_free(err);
        sqlite3_close(g_db);
        g_db = NULL;
        pthread_mutex_unlock(&db_mutex);
        return -1;
    }

    pthread_mutex_unlock(&db_mutex);
    return 0;
}

void stats_close_db(void)
{
    pthread_mutex_lock(&db_mutex);
    if (g_db) {
        sqlite3_close(g_db);
        g_db = NULL;
    }
    pthread_mutex_unlock(&db_mutex);
}

/* ------------------------------------------------------------------ */
/* Snapshot recording                                                  */
/* ------------------------------------------------------------------ */

int stats_record_snapshot(int capacity, float temperature, float voltage_mv,
                          float current_ma, const char *status, const char *mode)
{
    int rc;
    char now[32];
    sqlite3_stmt *stmt = NULL;
    static const char *sql =
        "INSERT INTO battery_snapshots "
        "(timestamp, capacity, temperature, voltage_mv, current_ma, status, mode) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)";

    utc_now(now, sizeof(now));

    pthread_mutex_lock(&db_mutex);
    if (!g_db) { pthread_mutex_unlock(&db_mutex); return -1; }

    rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) { pthread_mutex_unlock(&db_mutex); return -1; }

    sqlite3_bind_text(stmt, 1, now, -1, SQLITE_STATIC);
    if (capacity < 0)
        sqlite3_bind_null(stmt, 2);
    else
        sqlite3_bind_int(stmt, 2, capacity);
    if (temperature < -999.0f)
        sqlite3_bind_null(stmt, 3);
    else
        sqlite3_bind_double(stmt, 3, (double)temperature);
    if (voltage_mv < 0.0f)
        sqlite3_bind_null(stmt, 4);
    else
        sqlite3_bind_double(stmt, 4, (double)voltage_mv);
    if (current_ma < -99999.0f)
        sqlite3_bind_null(stmt, 5);
    else
        sqlite3_bind_double(stmt, 5, (double)current_ma);
    sqlite3_bind_text(stmt, 6, status ? status : "Unknown", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 7, mode   ? mode   : "normal",  -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&db_mutex);

    return (rc == SQLITE_DONE) ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/* Session tracking                                                    */
/* ------------------------------------------------------------------ */

int stats_start_session(int start_level, const char *mode, sqlite3_int64 *out_id)
{
    int rc;
    char now[32];
    sqlite3_stmt *stmt = NULL;
    static const char *sql =
        "INSERT INTO charging_sessions (start_time, start_level, mode) VALUES (?, ?, ?)";

    utc_now(now, sizeof(now));

    pthread_mutex_lock(&db_mutex);
    if (!g_db) { pthread_mutex_unlock(&db_mutex); return -1; }

    rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) { pthread_mutex_unlock(&db_mutex); return -1; }

    sqlite3_bind_text(stmt, 1, now, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, start_level);
    sqlite3_bind_text(stmt, 3, mode ? mode : "normal", -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_DONE && out_id) *out_id = sqlite3_last_insert_rowid(g_db);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&db_mutex);

    return (rc == SQLITE_DONE) ? 0 : -1;
}

int stats_end_session(sqlite3_int64 session_id, int end_level, float max_temp)
{
    int rc;
    char now[32];
    sqlite3_stmt *stmt = NULL;
    int start_level = 0;
    char start_time[32];
    int duration_s = 0;
    int delta_level = 0;
    double efficiency = 0.0;

    utc_now(now, sizeof(now));

    pthread_mutex_lock(&db_mutex);
    if (!g_db) { pthread_mutex_unlock(&db_mutex); return -1; }

    /* Fetch start info */
    rc = sqlite3_prepare_v2(g_db,
        "SELECT start_time, start_level FROM charging_sessions WHERE id = ?",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) { pthread_mutex_unlock(&db_mutex); return -1; }
    sqlite3_bind_int64(stmt, 1, session_id);
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&db_mutex);
        return -1;
    }
    strncpy(start_time, (const char *)sqlite3_column_text(stmt, 0), sizeof(start_time) - 1);
    start_time[sizeof(start_time)-1] = '\0';
    start_level = sqlite3_column_int(stmt, 1);
    sqlite3_finalize(stmt);

    /* Calculate duration and efficiency using float arithmetic (Bug fix) */
    {
        struct tm tm_s, tm_e;
        time_t t_s, t_e;
        memset(&tm_s, 0, sizeof(tm_s));
        memset(&tm_e, 0, sizeof(tm_e));
        sscanf(start_time, "%d-%d-%dT%d:%d:%d",
               &tm_s.tm_year, &tm_s.tm_mon, &tm_s.tm_mday,
               &tm_s.tm_hour, &tm_s.tm_min, &tm_s.tm_sec);
        tm_s.tm_year -= 1900; tm_s.tm_mon -= 1;
        t_s = timegm(&tm_s);

        sscanf(now, "%d-%d-%dT%d:%d:%d",
               &tm_e.tm_year, &tm_e.tm_mon, &tm_e.tm_mday,
               &tm_e.tm_hour, &tm_e.tm_min, &tm_e.tm_sec);
        tm_e.tm_year -= 1900; tm_e.tm_mon -= 1;
        t_e = timegm(&tm_e);

        duration_s = (int)(t_e - t_s);
        delta_level = end_level - start_level;
        /* Bug fix: use float division to avoid integer truncation when duration_s < 3600 */
        if (duration_s > 0) {
            efficiency = (double)((float)delta_level / ((float)duration_s / 3600.0f));
        }
    }

    rc = sqlite3_prepare_v2(g_db,
        "UPDATE charging_sessions "
        "SET end_time=?, end_level=?, max_temp=?, duration_s=?, efficiency=? "
        "WHERE id=?",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) { pthread_mutex_unlock(&db_mutex); return -1; }

    sqlite3_bind_text(stmt, 1, now, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, end_level);
    sqlite3_bind_double(stmt, 3, (double)max_temp);
    sqlite3_bind_int(stmt, 4, duration_s);
    sqlite3_bind_double(stmt, 5, efficiency);
    sqlite3_bind_int64(stmt, 6, session_id);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&db_mutex);

    return (rc == SQLITE_DONE) ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/* Stats queries                                                       */
/* ------------------------------------------------------------------ */

/* Build a JSON array from a prepared statement.
 * Columns: text names from stmt's column names, values serialized as JSON.
 * Caller must free *out_json. */
static int stmt_to_json_array(sqlite3_stmt *stmt, char **out_json)
{
    char *buf = NULL;
    size_t cap = 0, len = 0;
    int first_row = 1;
    int rc;

    if (buf_append(&buf, &cap, &len, "[") != 0) return -1;

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        int ncols = sqlite3_column_count(stmt);
        int c;

        if (!first_row) {
            if (buf_append(&buf, &cap, &len, ",") != 0) { free(buf); return -1; }
        }
        first_row = 0;

        if (buf_append(&buf, &cap, &len, "{") != 0) { free(buf); return -1; }

        for (c = 0; c < ncols; c++) {
            const char *col_name = sqlite3_column_name(stmt, c);
            int col_type = sqlite3_column_type(stmt, c);

            if (c > 0) {
                if (buf_append(&buf, &cap, &len, ",") != 0) { free(buf); return -1; }
            }

            /* key */
            if (buf_append(&buf, &cap, &len, "\"%s\":", col_name) != 0) { free(buf); return -1; }

            /* value */
            switch (col_type) {
                case SQLITE_INTEGER:
                    if (buf_append(&buf, &cap, &len, "%lld",
                                   (long long)sqlite3_column_int64(stmt, c)) != 0) { free(buf); return -1; }
                    break;
                case SQLITE_FLOAT:
                    if (buf_append(&buf, &cap, &len, "%.4g",
                                   sqlite3_column_double(stmt, c)) != 0) { free(buf); return -1; }
                    break;
                case SQLITE_TEXT: {
                    const char *txt = (const char *)sqlite3_column_text(stmt, c);
                    /* Simple JSON string escaping */
                    if (buf_append(&buf, &cap, &len, "\"") != 0) { free(buf); return -1; }
                    for (; txt && *txt; txt++) {
                        if (*txt == '"') {
                            if (buf_append(&buf, &cap, &len, "\\\"") != 0) { free(buf); return -1; }
                        } else if (*txt == '\\') {
                            if (buf_append(&buf, &cap, &len, "\\\\") != 0) { free(buf); return -1; }
                        } else if (*txt == '\n') {
                            if (buf_append(&buf, &cap, &len, "\\n") != 0) { free(buf); return -1; }
                        } else if (*txt == '\r') {
                            if (buf_append(&buf, &cap, &len, "\\r") != 0) { free(buf); return -1; }
                        } else {
                            if (buf_append(&buf, &cap, &len, "%c", *txt) != 0) { free(buf); return -1; }
                        }
                    }
                    if (buf_append(&buf, &cap, &len, "\"") != 0) { free(buf); return -1; }
                    break;
                }
                default: /* NULL */
                    if (buf_append(&buf, &cap, &len, "null") != 0) { free(buf); return -1; }
                    break;
            }
        }

        if (buf_append(&buf, &cap, &len, "}") != 0) { free(buf); return -1; }
    }

    if (buf_append(&buf, &cap, &len, "]") != 0) { free(buf); return -1; }
    *out_json = buf;
    return 0;
}

int stats_get_daily(int days, char **out_json)
{
    char since[32];
    time_t t;
    struct tm tm_utc;
    sqlite3_stmt *stmt = NULL;
    int rc;

    if (!out_json) return -1;

    t = time(NULL) - (time_t)days * 86400;
    gmtime_r(&t, &tm_utc);
    strftime(since, sizeof(since), "%Y-%m-%dT%H:%M:%S", &tm_utc);

    pthread_mutex_lock(&db_mutex);
    if (!g_db) { pthread_mutex_unlock(&db_mutex); return -1; }

    rc = sqlite3_prepare_v2(g_db,
        "SELECT DATE(start_time) AS day, COUNT(*) AS sessions, "
        "AVG(efficiency) AS avg_efficiency, SUM(duration_s) AS total_duration_s, "
        "MAX(max_temp) AS max_temp "
        "FROM charging_sessions "
        "WHERE start_time >= ? AND end_time IS NOT NULL "
        "GROUP BY DATE(start_time) ORDER BY day ASC",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) { pthread_mutex_unlock(&db_mutex); return -1; }
    sqlite3_bind_text(stmt, 1, since, -1, SQLITE_STATIC);

    rc = stmt_to_json_array(stmt, out_json);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&db_mutex);
    return rc;
}

int stats_get_weekly(char **out_json)
{
    char since[32];
    time_t t;
    struct tm tm_utc;
    sqlite3_stmt *stmt = NULL;
    int rc;

    if (!out_json) return -1;

    t = time(NULL) - (time_t)12 * 7 * 86400;
    gmtime_r(&t, &tm_utc);
    strftime(since, sizeof(since), "%Y-%m-%dT%H:%M:%S", &tm_utc);

    pthread_mutex_lock(&db_mutex);
    if (!g_db) { pthread_mutex_unlock(&db_mutex); return -1; }

    rc = sqlite3_prepare_v2(g_db,
        "SELECT STRFTIME('%Y-W%W', start_time) AS week, COUNT(*) AS sessions, "
        "AVG(efficiency) AS avg_efficiency, SUM(duration_s) AS total_duration_s, "
        "MAX(max_temp) AS max_temp "
        "FROM charging_sessions "
        "WHERE start_time >= ? AND end_time IS NOT NULL "
        "GROUP BY week ORDER BY week ASC",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) { pthread_mutex_unlock(&db_mutex); return -1; }
    sqlite3_bind_text(stmt, 1, since, -1, SQLITE_STATIC);

    rc = stmt_to_json_array(stmt, out_json);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&db_mutex);
    return rc;
}

int stats_get_monthly(char **out_json)
{
    char since[32];
    time_t t;
    struct tm tm_utc;
    sqlite3_stmt *stmt = NULL;
    int rc;

    if (!out_json) return -1;

    t = time(NULL) - (time_t)365 * 86400;
    gmtime_r(&t, &tm_utc);
    strftime(since, sizeof(since), "%Y-%m-%dT%H:%M:%S", &tm_utc);

    pthread_mutex_lock(&db_mutex);
    if (!g_db) { pthread_mutex_unlock(&db_mutex); return -1; }

    rc = sqlite3_prepare_v2(g_db,
        "SELECT STRFTIME('%Y-%m', start_time) AS month, COUNT(*) AS sessions, "
        "AVG(efficiency) AS avg_efficiency, SUM(duration_s) AS total_duration_s, "
        "MAX(max_temp) AS max_temp "
        "FROM charging_sessions "
        "WHERE start_time >= ? AND end_time IS NOT NULL "
        "GROUP BY month ORDER BY month ASC",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) { pthread_mutex_unlock(&db_mutex); return -1; }
    sqlite3_bind_text(stmt, 1, since, -1, SQLITE_STATIC);

    rc = stmt_to_json_array(stmt, out_json);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&db_mutex);
    return rc;
}

int stats_get_recent_snapshots(int limit, char **out_json)
{
    sqlite3_stmt *stmt = NULL;
    int rc;

    if (!out_json) return -1;

    pthread_mutex_lock(&db_mutex);
    if (!g_db) { pthread_mutex_unlock(&db_mutex); return -1; }

    rc = sqlite3_prepare_v2(g_db,
        "SELECT * FROM battery_snapshots ORDER BY timestamp DESC LIMIT ?",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) { pthread_mutex_unlock(&db_mutex); return -1; }
    sqlite3_bind_int(stmt, 1, limit);

    rc = stmt_to_json_array(stmt, out_json);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&db_mutex);
    return rc;
}

int stats_get_health_trend(char **out_json)
{
    sqlite3_stmt *stmt = NULL;
    int rc;
    double avg_temp = 0.0, max_temp_val = 0.0;
    int total_snapshots = 0, total_sessions = 0;
    int health_score = 100;
    char *buf = NULL;
    size_t cap = 0, len = 0;

    if (!out_json) return -1;

    pthread_mutex_lock(&db_mutex);
    if (!g_db) { pthread_mutex_unlock(&db_mutex); return -1; }

    rc = sqlite3_prepare_v2(g_db,
        "SELECT AVG(temperature) AS avg_temp, MAX(temperature) AS max_temp, COUNT(*) AS total "
        "FROM battery_snapshots",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) { pthread_mutex_unlock(&db_mutex); return -1; }

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        if (sqlite3_column_type(stmt, 0) != SQLITE_NULL) avg_temp = sqlite3_column_double(stmt, 0);
        if (sqlite3_column_type(stmt, 1) != SQLITE_NULL) max_temp_val = sqlite3_column_double(stmt, 1);
        total_snapshots = sqlite3_column_int(stmt, 2);
    }
    sqlite3_finalize(stmt);

    rc = sqlite3_prepare_v2(g_db,
        "SELECT COUNT(*) FROM charging_sessions WHERE end_time IS NOT NULL",
        -1, &stmt, NULL);
    if (rc == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW) {
        total_sessions = sqlite3_column_int(stmt, 0);
    }
    if (stmt) sqlite3_finalize(stmt);

    pthread_mutex_unlock(&db_mutex);

    /* Health score heuristic (mirrors stats.py) */
    if (avg_temp > 38.0) health_score -= 10;
    if (max_temp_val > 44.0) health_score -= 15;

    /* Bug fix: clamp to [0, 100] (original Python only had lower bound) */
    if (health_score < 0)   health_score = 0;
    if (health_score > 100) health_score = 100;

    if (buf_append(&buf, &cap, &len,
        "{\"estimated_health\":%d,\"avg_temp\":%.1f,\"max_temp\":%.1f,"
        "\"total_snapshots\":%d,\"total_sessions\":%d}",
        health_score, avg_temp, max_temp_val, total_snapshots, total_sessions) != 0) {
        return -1;
    }

    *out_json = buf;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Export                                                              */
/* ------------------------------------------------------------------ */

int stats_export_json(char **out_json)
{
    sqlite3_stmt *stmt = NULL;
    int rc;

    if (!out_json) return -1;

    pthread_mutex_lock(&db_mutex);
    if (!g_db) { pthread_mutex_unlock(&db_mutex); return -1; }

    rc = sqlite3_prepare_v2(g_db,
        "SELECT * FROM charging_sessions ORDER BY start_time",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) { pthread_mutex_unlock(&db_mutex); return -1; }

    rc = stmt_to_json_array(stmt, out_json);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&db_mutex);
    return rc;
}

int stats_export_csv(char **out_csv)
{
    sqlite3_stmt *stmt = NULL;
    int rc;
    int first_row = 1;
    char *buf = NULL;
    size_t cap = 0, len = 0;

    if (!out_csv) return -1;

    pthread_mutex_lock(&db_mutex);
    if (!g_db) { pthread_mutex_unlock(&db_mutex); return -1; }

    rc = sqlite3_prepare_v2(g_db,
        "SELECT * FROM charging_sessions ORDER BY start_time",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) { pthread_mutex_unlock(&db_mutex); return -1; }

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        int ncols = sqlite3_column_count(stmt);
        int c;

        if (first_row) {
            /* Header */
            for (c = 0; c < ncols; c++) {
                if (c > 0) buf_append(&buf, &cap, &len, ",");
                buf_append(&buf, &cap, &len, "%s", sqlite3_column_name(stmt, c));
            }
            buf_append(&buf, &cap, &len, "\n");
            first_row = 0;
        }

        for (c = 0; c < ncols; c++) {
            if (c > 0) buf_append(&buf, &cap, &len, ",");
            switch (sqlite3_column_type(stmt, c)) {
                case SQLITE_INTEGER:
                    buf_append(&buf, &cap, &len, "%lld", (long long)sqlite3_column_int64(stmt, c));
                    break;
                case SQLITE_FLOAT:
                    buf_append(&buf, &cap, &len, "%.4g", sqlite3_column_double(stmt, c));
                    break;
                case SQLITE_TEXT:
                    buf_append(&buf, &cap, &len, "%s", sqlite3_column_text(stmt, c));
                    break;
                default:
                    break;
            }
        }
        buf_append(&buf, &cap, &len, "\n");
    }

    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&db_mutex);

    if (!buf) {
        buf = (char *)malloc(1);
        if (!buf) return -1;
        buf[0] = '\0';
    }

    *out_csv = buf;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Pruning                                                             */
/* ------------------------------------------------------------------ */

int stats_prune_old_data(int retention_days)
{
    char cutoff[32];
    time_t t;
    struct tm tm_utc;
    int deleted = 0;
    char *err = NULL;
    sqlite3_stmt *stmt = NULL;
    int rc;

    t = time(NULL) - (time_t)retention_days * 86400;
    gmtime_r(&t, &tm_utc);
    strftime(cutoff, sizeof(cutoff), "%Y-%m-%dT%H:%M:%S", &tm_utc);

    pthread_mutex_lock(&db_mutex);
    if (!g_db) { pthread_mutex_unlock(&db_mutex); return 0; }

    rc = sqlite3_prepare_v2(g_db,
        "DELETE FROM charging_sessions WHERE start_time < ?",
        -1, &stmt, NULL);
    if (rc == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, cutoff, -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        deleted += sqlite3_changes(g_db);
        sqlite3_finalize(stmt);
    }

    rc = sqlite3_prepare_v2(g_db,
        "DELETE FROM battery_snapshots WHERE timestamp < ?",
        -1, &stmt, NULL);
    if (rc == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, cutoff, -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        deleted += sqlite3_changes(g_db);
        sqlite3_finalize(stmt);
    }

    (void)err;
    pthread_mutex_unlock(&db_mutex);
    return deleted;
}
