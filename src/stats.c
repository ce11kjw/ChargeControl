#include "stats.h"
#include "cJSON.h"

#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Maximum plausible efficiency value (% per hour).  Values outside
   [-MAX_EFFICIENCY_THRESHOLD, +MAX_EFFICIENCY_THRESHOLD] are clamped to 0
   to guard against division by very small duration_s values. */
#define MAX_EFFICIENCY_THRESHOLD 200.0

/* ── globals ─────────────────────────────────────────────── */

pthread_mutex_t g_db_mutex = PTHREAD_MUTEX_INITIALIZER;

static char s_db_path[600] = "";

/* ── helpers ─────────────────────────────────────────────── */

static sqlite3 *open_db(void)
{
    sqlite3 *db = NULL;
    if (sqlite3_open(s_db_path, &db) != SQLITE_OK) {
        if (db) sqlite3_close(db);
        return NULL;
    }
    sqlite3_busy_timeout(db, 5000);
    return db;
}

static void utc_now(char *buf, size_t sz)
{
    time_t t = time(NULL);
    struct tm *utc = gmtime(&t);
    strftime(buf, sz, "%Y-%m-%dT%H:%M:%S", utc);
}

/* Parse an ISO-8601 timestamp, accepting both "2024-01-01T12:00:00" and
   "2024-01-01T12:00:00Z".  Returns (time_t)-1 on failure.
   Bug fix: original Python used datetime.fromisoformat() which fails on 'Z'. */
static time_t parse_iso8601(const char *s)
{
    struct tm tm;
    memset(&tm, 0, sizeof(tm));
    /* Try with Z suffix first */
    char *p = strptime(s, "%Y-%m-%dT%H:%M:%SZ", &tm);
    if (!p) p = strptime(s, "%Y-%m-%dT%H:%M:%S", &tm);
    if (!p) return (time_t)-1;
#ifdef _GNU_SOURCE
    return timegm(&tm);
#else
    /* portable fallback */
    tm.tm_isdst = 0;
    char *tz_save = getenv("TZ");
    setenv("TZ", "UTC", 1);
    tzset();
    time_t t = mktime(&tm);
    if (tz_save) setenv("TZ", tz_save, 1);
    else         unsetenv("TZ");
    tzset();
    return t;
#endif
}

/* Append a formatted string to a dynamically growing buffer.
   *buf must be NULL-initialised. Returns new pointer (NULL on OOM). */
static char *buf_append(char *buf, size_t *len, size_t *cap, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));

static char *buf_append(char *buf, size_t *len, size_t *cap, const char *fmt, ...)
{
    char tmp[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n < 0) return buf;
    size_t need = *len + (size_t)n + 1;
    if (need > *cap) {
        size_t newcap = need * 2 + 64;
        char *nb = realloc(buf, newcap);
        if (!nb) return buf;
        buf = nb;
        *cap = newcap;
    }
    memcpy(buf + *len, tmp, (size_t)n + 1);
    *len += (size_t)n;
    return buf;
}
#include <stdarg.h>

/* ── public API ──────────────────────────────────────────── */

int stats_init_db(const char *db_path)
{
    snprintf(s_db_path, sizeof(s_db_path), "%s", db_path);

    sqlite3 *db = open_db();
    if (!db) return -1;

    const char *sql =
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

    int rc = sqlite3_exec(db, sql, NULL, NULL, NULL);
    sqlite3_close(db);
    return (rc == SQLITE_OK) ? 0 : -1;
}

long long stats_record_snapshot(int capacity, double temperature,
                                double voltage_mv, double current_ma,
                                const char *status, const char *mode)
{
    char now[32];
    utc_now(now, sizeof(now));

    pthread_mutex_lock(&g_db_mutex);
    sqlite3 *db = open_db();
    if (!db) { pthread_mutex_unlock(&g_db_mutex); return -1; }

    sqlite3_stmt *stmt;
    const char *sql =
        "INSERT INTO battery_snapshots"
        " (timestamp,capacity,temperature,voltage_mv,current_ma,status,mode)"
        " VALUES (?,?,?,?,?,?,?)";

    long long rowid = -1;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text  (stmt, 1, now,         -1, SQLITE_STATIC);
        sqlite3_bind_int   (stmt, 2, capacity);
        sqlite3_bind_double(stmt, 3, temperature);
        sqlite3_bind_double(stmt, 4, voltage_mv);
        sqlite3_bind_double(stmt, 5, current_ma);
        sqlite3_bind_text  (stmt, 6, status,      -1, SQLITE_STATIC);
        sqlite3_bind_text  (stmt, 7, mode,        -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_DONE)
            rowid = sqlite3_last_insert_rowid(db);
        sqlite3_finalize(stmt);
    }
    sqlite3_close(db);
    pthread_mutex_unlock(&g_db_mutex);
    return rowid;
}

long long stats_start_session(int start_level, const char *mode)
{
    char now[32];
    utc_now(now, sizeof(now));

    pthread_mutex_lock(&g_db_mutex);
    sqlite3 *db = open_db();
    if (!db) { pthread_mutex_unlock(&g_db_mutex); return -1; }

    sqlite3_stmt *stmt;
    const char *sql =
        "INSERT INTO charging_sessions (start_time, start_level, mode)"
        " VALUES (?,?,?)";

    long long rowid = -1;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, now,   -1, SQLITE_STATIC);
        sqlite3_bind_int (stmt, 2, start_level);
        sqlite3_bind_text(stmt, 3, mode,  -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_DONE)
            rowid = sqlite3_last_insert_rowid(db);
        sqlite3_finalize(stmt);
    }
    sqlite3_close(db);
    pthread_mutex_unlock(&g_db_mutex);
    return rowid;
}

int stats_end_session(long long session_id, int end_level, double max_temp)
{
    char now[32];
    utc_now(now, sizeof(now));

    pthread_mutex_lock(&g_db_mutex);
    sqlite3 *db = open_db();
    if (!db) { pthread_mutex_unlock(&g_db_mutex); return -1; }

    /* Fetch start_time and start_level */
    sqlite3_stmt *sel;
    const char *sel_sql =
        "SELECT start_time, start_level FROM charging_sessions WHERE id=?";
    char   start_time[32] = "";
    int    start_level    = 0;
    int    found          = 0;

    if (sqlite3_prepare_v2(db, sel_sql, -1, &sel, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(sel, 1, session_id);
        if (sqlite3_step(sel) == SQLITE_ROW) {
            const unsigned char *t = sqlite3_column_text(sel, 0);
            if (t) snprintf(start_time, sizeof(start_time), "%s", (const char *)t);
            start_level = sqlite3_column_int(sel, 1);
            found = 1;
        }
        sqlite3_finalize(sel);
    }

    if (!found) {
        sqlite3_close(db);
        pthread_mutex_unlock(&g_db_mutex);
        return -1;
    }

    /* Bug fix: parse ISO-8601 with optional Z suffix */
    time_t t_start = parse_iso8601(start_time);
    time_t t_end   = time(NULL);
    int duration_s = (t_start != (time_t)-1) ? (int)(t_end - t_start) : 0;

    int    delta_level = end_level - start_level;
    double efficiency  = 0.0;
    if (duration_s > 0) {
        double hours = duration_s / 3600.0;
        efficiency = (hours > 0) ? delta_level / hours : 0.0;
        /* Bug fix: clamp runaway values caused by very short sessions */
        if (efficiency > MAX_EFFICIENCY_THRESHOLD || efficiency < -MAX_EFFICIENCY_THRESHOLD)
            efficiency = 0.0;
    }

    sqlite3_stmt *upd;
    const char *upd_sql =
        "UPDATE charging_sessions"
        " SET end_time=?,end_level=?,max_temp=?,duration_s=?,efficiency=?"
        " WHERE id=?";

    int rc = -1;
    if (sqlite3_prepare_v2(db, upd_sql, -1, &upd, NULL) == SQLITE_OK) {
        sqlite3_bind_text  (upd, 1, now,         -1, SQLITE_STATIC);
        sqlite3_bind_int   (upd, 2, end_level);
        sqlite3_bind_double(upd, 3, max_temp);
        sqlite3_bind_int   (upd, 4, duration_s);
        sqlite3_bind_double(upd, 5, efficiency);
        sqlite3_bind_int64 (upd, 6, session_id);
        if (sqlite3_step(upd) == SQLITE_DONE) rc = 0;
        sqlite3_finalize(upd);
    }
    sqlite3_close(db);
    pthread_mutex_unlock(&g_db_mutex);
    return rc;
}

/* ── query helpers that build JSON ──────────────────────── */

/* Execute a SELECT query and return the result rows as a JSON array string.
   Column names are taken directly from the query. */
static char *query_to_json_array(const char *query, const char *param)
{
    pthread_mutex_lock(&g_db_mutex);
    sqlite3 *db = open_db();
    if (!db) { pthread_mutex_unlock(&g_db_mutex); return NULL; }

    sqlite3_stmt *stmt;
    char *result = NULL;

    if (sqlite3_prepare_v2(db, query, -1, &stmt, NULL) == SQLITE_OK) {
        if (param) sqlite3_bind_text(stmt, 1, param, -1, SQLITE_STATIC);

        cJSON *arr = cJSON_CreateArray();
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int ncols = sqlite3_column_count(stmt);
            cJSON *row = cJSON_CreateObject();
            for (int i = 0; i < ncols; i++) {
                const char *col = sqlite3_column_name(stmt, i);
                int ctype = sqlite3_column_type(stmt, i);
                if (ctype == SQLITE_NULL) {
                    cJSON_AddNullToObject(row, col);
                } else if (ctype == SQLITE_INTEGER) {
                    cJSON_AddNumberToObject(row, col,
                        (double)sqlite3_column_int64(stmt, i));
                } else if (ctype == SQLITE_FLOAT) {
                    cJSON_AddNumberToObject(row, col,
                        sqlite3_column_double(stmt, i));
                } else {
                    const unsigned char *t = sqlite3_column_text(stmt, i);
                    cJSON_AddStringToObject(row, col,
                        t ? (const char *)t : "");
                }
            }
            cJSON_AddItemToArray(arr, row);
        }
        sqlite3_finalize(stmt);
        result = cJSON_PrintUnformatted(arr);
        cJSON_Delete(arr);
    }
    sqlite3_close(db);
    pthread_mutex_unlock(&g_db_mutex);
    return result;
}

char *stats_get_daily_stats(int days)
{
    char since[32];
    time_t t = time(NULL) - (time_t)days * 86400;
    struct tm *utc = gmtime(&t);
    strftime(since, sizeof(since), "%Y-%m-%dT%H:%M:%S", utc);

    const char *sql =
        "SELECT DATE(start_time) AS day,"
        "       COUNT(*) AS sessions,"
        "       AVG(efficiency) AS avg_efficiency,"
        "       SUM(duration_s) AS total_duration_s,"
        "       MAX(max_temp) AS max_temp"
        " FROM charging_sessions"
        " WHERE start_time >= ? AND end_time IS NOT NULL"
        " GROUP BY DATE(start_time)"
        " ORDER BY day ASC";

    return query_to_json_array(sql, since);
}

char *stats_get_weekly_stats(void)
{
    char since[32];
    time_t t = time(NULL) - (time_t)84 * 86400; /* 12 weeks */
    struct tm *utc = gmtime(&t);
    strftime(since, sizeof(since), "%Y-%m-%dT%H:%M:%S", utc);

    const char *sql =
        "SELECT STRFTIME('%Y-W%W', start_time) AS week,"
        "       COUNT(*) AS sessions,"
        "       AVG(efficiency) AS avg_efficiency,"
        "       SUM(duration_s) AS total_duration_s,"
        "       MAX(max_temp) AS max_temp"
        " FROM charging_sessions"
        " WHERE start_time >= ? AND end_time IS NOT NULL"
        " GROUP BY week"
        " ORDER BY week ASC";

    return query_to_json_array(sql, since);
}

char *stats_get_monthly_stats(void)
{
    char since[32];
    time_t t = time(NULL) - (time_t)365 * 86400;
    struct tm *utc = gmtime(&t);
    strftime(since, sizeof(since), "%Y-%m-%dT%H:%M:%S", utc);

    const char *sql =
        "SELECT STRFTIME('%Y-%m', start_time) AS month,"
        "       COUNT(*) AS sessions,"
        "       AVG(efficiency) AS avg_efficiency,"
        "       SUM(duration_s) AS total_duration_s,"
        "       MAX(max_temp) AS max_temp"
        " FROM charging_sessions"
        " WHERE start_time >= ? AND end_time IS NOT NULL"
        " GROUP BY month"
        " ORDER BY month ASC";

    return query_to_json_array(sql, since);
}

char *stats_get_recent_snapshots(int limit)
{
    pthread_mutex_lock(&g_db_mutex);
    sqlite3 *db = open_db();
    if (!db) { pthread_mutex_unlock(&g_db_mutex); return NULL; }

    sqlite3_stmt *stmt;
    const char *sql =
        "SELECT * FROM battery_snapshots ORDER BY timestamp DESC LIMIT ?";

    cJSON *arr = cJSON_CreateArray();
    /* Collect in reverse, then we'll reverse the array to return oldest-first */
    cJSON *tmp_arr = cJSON_CreateArray();

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, limit);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int ncols = sqlite3_column_count(stmt);
            cJSON *row = cJSON_CreateObject();
            for (int i = 0; i < ncols; i++) {
                const char *col = sqlite3_column_name(stmt, i);
                int ctype = sqlite3_column_type(stmt, i);
                if (ctype == SQLITE_NULL)
                    cJSON_AddNullToObject(row, col);
                else if (ctype == SQLITE_INTEGER)
                    cJSON_AddNumberToObject(row, col,
                        (double)sqlite3_column_int64(stmt, i));
                else if (ctype == SQLITE_FLOAT)
                    cJSON_AddNumberToObject(row, col,
                        sqlite3_column_double(stmt, i));
                else {
                    const unsigned char *t = sqlite3_column_text(stmt, i);
                    cJSON_AddStringToObject(row, col,
                        t ? (const char *)t : "");
                }
            }
            cJSON_AddItemToArray(tmp_arr, row);
        }
        sqlite3_finalize(stmt);
    }
    sqlite3_close(db);
    pthread_mutex_unlock(&g_db_mutex);

    /* Reverse: iterate tmp_arr from end to start */
    int n = cJSON_GetArraySize(tmp_arr);
    for (int i = n - 1; i >= 0; i--) {
        cJSON *item = cJSON_GetArrayItem(tmp_arr, i);
        cJSON_AddItemToArray(arr, cJSON_Duplicate(item, 1));
    }
    cJSON_Delete(tmp_arr);

    char *s = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    return s;
}

char *stats_get_battery_health(void)
{
    pthread_mutex_lock(&g_db_mutex);
    sqlite3 *db = open_db();
    if (!db) { pthread_mutex_unlock(&g_db_mutex); return NULL; }

    double avg_temp = 0, max_temp = 0;
    long long total_snapshots = 0, total_sessions = 0;

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db,
            "SELECT AVG(temperature), MAX(temperature), COUNT(*)"
            " FROM battery_snapshots", -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            avg_temp        = sqlite3_column_double(stmt, 0);
            max_temp        = sqlite3_column_double(stmt, 1);
            total_snapshots = sqlite3_column_int64 (stmt, 2);
        }
        sqlite3_finalize(stmt);
    }
    if (sqlite3_prepare_v2(db,
            "SELECT COUNT(*) FROM charging_sessions WHERE end_time IS NOT NULL",
            -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW)
            total_sessions = sqlite3_column_int64(stmt, 0);
        sqlite3_finalize(stmt);
    }
    sqlite3_close(db);
    pthread_mutex_unlock(&g_db_mutex);

    int health_score = 100;
    if (avg_temp > 38) health_score -= 10;
    if (max_temp > 44) health_score -= 15;
    if (health_score < 0) health_score = 0;

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "estimated_health", health_score);
    cJSON_AddNumberToObject(root, "avg_temp",         (int)(avg_temp * 10 + 0.5) / 10.0);
    cJSON_AddNumberToObject(root, "max_temp",         (int)(max_temp * 10 + 0.5) / 10.0);
    cJSON_AddNumberToObject(root, "total_snapshots",  (double)total_snapshots);
    cJSON_AddNumberToObject(root, "total_sessions",   (double)total_sessions);
    char *s = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return s;
}

char *stats_export_csv(void)
{
    pthread_mutex_lock(&g_db_mutex);
    sqlite3 *db = open_db();
    if (!db) { pthread_mutex_unlock(&g_db_mutex); return NULL; }

    sqlite3_stmt *stmt;
    const char *sql =
        "SELECT id,start_time,end_time,start_level,end_level,"
        "       mode,max_temp,duration_s,efficiency"
        " FROM charging_sessions ORDER BY start_time";

    char   *buf = NULL;
    size_t  len = 0, cap = 0;

    buf = buf_append(buf, &len, &cap,
        "id,start_time,end_time,start_level,end_level,"
        "mode,max_temp,duration_s,efficiency\n");

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            long long id   = sqlite3_column_int64(stmt, 0);
            const unsigned char *st = sqlite3_column_text(stmt, 1);
            const unsigned char *et = sqlite3_column_text(stmt, 2);
            int sl = sqlite3_column_int(stmt, 3);
            int el = sqlite3_column_int(stmt, 4);
            const unsigned char *mo = sqlite3_column_text(stmt, 5);
            double mt  = sqlite3_column_double(stmt, 6);
            int    dur = sqlite3_column_int(stmt, 7);
            double eff = sqlite3_column_double(stmt, 8);

            buf = buf_append(buf, &len, &cap,
                "%lld,%s,%s,%d,%d,%s,%.1f,%d,%.2f\n",
                id,
                st  ? (const char *)st : "",
                et  ? (const char *)et : "",
                sl, el,
                mo  ? (const char *)mo : "",
                mt, dur, eff);
        }
        sqlite3_finalize(stmt);
    }
    sqlite3_close(db);
    pthread_mutex_unlock(&g_db_mutex);
    return buf;
}

char *stats_export_json(void)
{
    const char *sql =
        "SELECT * FROM charging_sessions ORDER BY start_time";
    return query_to_json_array(sql, NULL);
}

int stats_prune_old_data(int retention_days)
{
    char cutoff[32];
    time_t t = time(NULL) - (time_t)retention_days * 86400;
    struct tm *utc = gmtime(&t);
    strftime(cutoff, sizeof(cutoff), "%Y-%m-%dT%H:%M:%S", utc);

    pthread_mutex_lock(&g_db_mutex);
    sqlite3 *db = open_db();
    if (!db) { pthread_mutex_unlock(&g_db_mutex); return -1; }

    int deleted = 0;
    sqlite3_stmt *stmt;

    if (sqlite3_prepare_v2(db,
            "DELETE FROM charging_sessions WHERE start_time < ?",
            -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, cutoff, -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        deleted += sqlite3_changes(db);
        sqlite3_finalize(stmt);
    }
    if (sqlite3_prepare_v2(db,
            "DELETE FROM battery_snapshots WHERE timestamp < ?",
            -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, cutoff, -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        deleted += sqlite3_changes(db);
        sqlite3_finalize(stmt);
    }
    sqlite3_close(db);
    pthread_mutex_unlock(&g_db_mutex);
    return deleted;
}
