#include "http_server.h"
#include "charge_control.h"
#include "stats.h"
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>

#define MAX_REQUEST_SIZE  (64 * 1024)
#define MAX_BODY_SIZE     (16 * 1024)
#define RESPONSE_BUF_SIZE (64 * 1024)

/* ------------------------------------------------------------------ */
/* Global state                                                        */
/* ------------------------------------------------------------------ */

static volatile int        g_running   = 0;
static int                 g_server_fd = -1;
static http_server_config_t g_cfg;

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static const char *mime_for_path(const char *path)
{
    const char *dot = strrchr(path, '.');
    if (!dot) return "application/octet-stream";
    if (strcmp(dot, ".html") == 0) return "text/html; charset=utf-8";
    if (strcmp(dot, ".css")  == 0) return "text/css; charset=utf-8";
    if (strcmp(dot, ".js")   == 0) return "application/javascript; charset=utf-8";
    if (strcmp(dot, ".json") == 0) return "application/json; charset=utf-8";
    if (strcmp(dot, ".ico")  == 0) return "image/x-icon";
    return "application/octet-stream";
}

/* Send a complete response with given status, content type, and body. */
static void send_response(int fd, int status, const char *status_text,
                          const char *content_type, const char *body, size_t body_len)
{
    char header[512];
    int hlen;

    hlen = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, DELETE, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type\r\n"
        "Connection: close\r\n"
        "\r\n",
        status, status_text,
        content_type,
        body_len);

    send(fd, header, (size_t)hlen, MSG_NOSIGNAL);
    if (body && body_len > 0) {
        send(fd, body, body_len, MSG_NOSIGNAL);
    }
}

static void send_json(int fd, int status, const char *body)
{
    send_response(fd, status, status == 200 ? "OK" : "Error",
                  "application/json; charset=utf-8",
                  body, body ? strlen(body) : 0);
}

static void send_error_json(int fd, int status, const char *message)
{
    char buf[256];
    snprintf(buf, sizeof(buf), "{\"error\":\"%s\"}", message);
    send_json(fd, status, buf);
}

/* ------------------------------------------------------------------ */
/* Static file serving (with path-traversal protection)               */
/* ------------------------------------------------------------------ */

static void serve_static(int fd, const char *url_path)
{
    char file_path[4096 + 2048 + 2];  /* webroot + '/' + url_path */
    char *buf = NULL;
    FILE *f = NULL;
    long flen = 0;
    size_t nread = 0;
    size_t wlen, rlen;

    /* Bug fix: reject paths containing ".." to prevent path traversal attacks */
    if (strstr(url_path, "..") != NULL) {
        send_error_json(fd, 403, "Forbidden");
        return;
    }

    /* Map URL to file using strncat to avoid -Wformat-truncation */
    wlen = strlen(g_cfg.webroot);
    if (wlen >= sizeof(file_path) - 2) { send_error_json(fd, 500, "Internal error"); return; }
    memcpy(file_path, g_cfg.webroot, wlen);
    file_path[wlen] = '/';

    if (strcmp(url_path, "/") == 0 || strcmp(url_path, "/index.html") == 0) {
        memcpy(file_path + wlen + 1, "index.html", 11);
    } else {
        /* Strip leading slash */
        const char *rel = url_path + 1;
        rlen = strlen(rel);
        if (wlen + 1 + rlen + 1 > sizeof(file_path)) { send_error_json(fd, 414, "URI too long"); return; }
        memcpy(file_path + wlen + 1, rel, rlen + 1);
    }

    f = fopen(file_path, "rb");
    if (!f) {
        send_error_json(fd, 404, "Not Found");
        return;
    }

    fseek(f, 0, SEEK_END);
    flen = ftell(f);
    rewind(f);

    if (flen < 0 || flen > (long)(8 * 1024 * 1024)) {
        fclose(f);
        send_error_json(fd, 500, "File too large");
        return;
    }

    buf = (char *)malloc((size_t)flen);
    if (!buf) { fclose(f); send_error_json(fd, 500, "Out of memory"); return; }

    nread = fread(buf, 1, (size_t)flen, f);
    fclose(f);

    send_response(fd, 200, "OK", mime_for_path(file_path), buf, nread);
    free(buf);
}

/* ------------------------------------------------------------------ */
/* Request parsing                                                     */
/* ------------------------------------------------------------------ */

typedef struct {
    char method[32];
    char path[2048];
    int  content_length;
    char body[MAX_BODY_SIZE + 1];
} http_request_t;

/* Read exactly n bytes from fd into buf. Returns 0 on success, -1 on error. */
static int read_exact(int fd, char *buf, int n)
{
    int total = 0;
    while (total < n) {
        int r = (int)recv(fd, buf + total, (size_t)(n - total), 0);
        if (r <= 0) return -1;
        total += r;
    }
    return 0;
}

/* Parse incoming HTTP request. Returns 0 on success, -1 on error. */
static int parse_request(int fd, http_request_t *req)
{
    char buf[MAX_REQUEST_SIZE];
    int total = 0;
    char *header_end = NULL;
    char *line = NULL;
    char *cl_ptr = NULL;

    memset(req, 0, sizeof(*req));
    req->content_length = -1;

    /* Read headers (stop at \r\n\r\n) */
    while (total < (int)sizeof(buf) - 1) {
        int r = (int)recv(fd, buf + total, 1, 0);
        if (r <= 0) return -1;
        total++;
        buf[total] = '\0';
        if (total >= 4 && memcmp(buf + total - 4, "\r\n\r\n", 4) == 0) break;
        if (total >= (int)sizeof(buf) - 2) return -1;
    }

    header_end = strstr(buf, "\r\n\r\n");
    if (!header_end) return -1;

    /* Parse request line */
    line = buf;
    {
        char *sp1 = strchr(line, ' ');
        char *sp2 = sp1 ? strchr(sp1 + 1, ' ') : NULL;
        size_t method_len, path_len;
        if (!sp1 || !sp2) return -1;

        /* Copy method (e.g. "GET") with explicit length to avoid format-truncation warning */
        method_len = (size_t)(sp1 - line);
        if (method_len >= sizeof(req->method)) method_len = sizeof(req->method) - 1;
        memcpy(req->method, line, method_len);
        req->method[method_len] = '\0';

        /* Copy path */
        *sp2 = '\0';
        path_len = strlen(sp1 + 1);
        if (path_len >= sizeof(req->path)) path_len = sizeof(req->path) - 1;
        memcpy(req->path, sp1 + 1, path_len);
        req->path[path_len] = '\0';
        *sp2 = ' ';
    }

    /* Parse Content-Length header (case-insensitive) */
    cl_ptr = strcasestr(buf, "\r\ncontent-length:");
    if (cl_ptr) {
        cl_ptr += strlen("\r\ncontent-length:");
        while (*cl_ptr == ' ') cl_ptr++;
        req->content_length = atoi(cl_ptr);
    }

    /* Bug fix: read body using Content-Length, not by looking for \r\n\r\n */
    if (req->content_length > 0) {
        int to_read = req->content_length;
        if (to_read > MAX_BODY_SIZE) to_read = MAX_BODY_SIZE;
        if (read_exact(fd, req->body, to_read) != 0) return -1;
        req->body[to_read] = '\0';
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* API handlers                                                        */
/* ------------------------------------------------------------------ */

static void handle_get_battery(int fd)
{
    battery_status_t bat;
    cJSON *obj = NULL;
    char *json = NULL;

    if (cc_get_battery_status(&bat) != 0) {
        send_error_json(fd, 500, "Failed to read battery status");
        return;
    }

    obj = cJSON_CreateObject();
    if (bat.capacity < 0)
        cJSON_AddNullToObject(obj, "capacity");
    else
        cJSON_AddNumberToObject(obj, "capacity", bat.capacity);

    cJSON_AddStringToObject(obj, "status",   bat.status);
    cJSON_AddStringToObject(obj, "health",   bat.health);

    if (bat.temperature < -900.0f)
        cJSON_AddNullToObject(obj, "temperature");
    else
        cJSON_AddNumberToObject(obj, "temperature", (double)bat.temperature);

    if (bat.voltage_mv < 0.0f)
        cJSON_AddNullToObject(obj, "voltage_mv");
    else
        cJSON_AddNumberToObject(obj, "voltage_mv", (double)bat.voltage_mv);

    if (bat.current_ma < -90000.0f)
        cJSON_AddNullToObject(obj, "current_ma");
    else
        cJSON_AddNumberToObject(obj, "current_ma", (double)bat.current_ma);

    cJSON_AddBoolToObject(obj, "charging_enabled", bat.charging_enabled);
    cJSON_AddStringToObject(obj, "timestamp", bat.timestamp);

    json = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);

    send_json(fd, 200, json);
    free(json);
}

static void handle_get_settings(int fd)
{
    cJSON *cfg = config_load(g_cfg.config_path);
    battery_status_t bat;
    cJSON *root = NULL;
    cJSON *bat_obj = NULL;
    char *json = NULL;

    root = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "config", cfg ? cfg : cJSON_CreateObject());

    bat_obj = cJSON_CreateObject();
    if (cc_get_battery_status(&bat) == 0) {
        if (bat.capacity < 0)
            cJSON_AddNullToObject(bat_obj, "capacity");
        else
            cJSON_AddNumberToObject(bat_obj, "capacity", bat.capacity);
        cJSON_AddStringToObject(bat_obj, "status", bat.status);
        cJSON_AddStringToObject(bat_obj, "health", bat.health);
        if (bat.temperature < -900.0f)
            cJSON_AddNullToObject(bat_obj, "temperature");
        else
            cJSON_AddNumberToObject(bat_obj, "temperature", (double)bat.temperature);
        cJSON_AddBoolToObject(bat_obj, "charging_enabled", bat.charging_enabled);
        cJSON_AddStringToObject(bat_obj, "timestamp", bat.timestamp);
    }
    cJSON_AddItemToObject(root, "battery", bat_obj);

    json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    send_json(fd, 200, json);
    free(json);
}

static void handle_post_charging_enable(int fd, const char *body)
{
    cJSON *req_json = cJSON_Parse(body);
    cJSON *enabled_item = NULL;
    int enabled = 0;

    if (!req_json) { send_error_json(fd, 400, "Invalid JSON"); return; }

    enabled_item = cJSON_GetObjectItem(req_json, "enabled");
    if (!enabled_item) { cJSON_Delete(req_json); send_error_json(fd, 400, "Missing 'enabled'"); return; }

    enabled = cJSON_IsTrue(enabled_item) ? 1 : 0;
    cJSON_Delete(req_json);

    if (cc_set_charging_enabled(enabled) != 0) {
        /* sysfs not available; still update config */
    }

    /* Persist setting to config */
    {
        cJSON *cfg = config_load(g_cfg.config_path);
        if (cfg) {
            cJSON *charging = cJSON_GetObjectItem(cfg, "charging");
            if (!charging) {
                charging = cJSON_CreateObject();
                cJSON_AddItemToObject(cfg, "charging", charging);
            }
            cJSON_DeleteItemFromObject(charging, "charging_enabled");
            cJSON_AddBoolToObject(charging, "charging_enabled", enabled);
            config_save(g_cfg.config_path, cfg);
            cJSON_Delete(cfg);
        }
    }

    send_json(fd, 200, "{\"success\":true}");
}

static void handle_post_charging_mode(int fd, const char *body)
{
    cJSON *req_json = cJSON_Parse(body);
    cJSON *mode_item = NULL;
    const char *mode = NULL;
    cJSON *cfg = NULL;

    if (!req_json) { send_error_json(fd, 400, "Invalid JSON"); return; }

    mode_item = cJSON_GetObjectItem(req_json, "mode");
    if (!cJSON_IsString(mode_item)) {
        cJSON_Delete(req_json);
        send_error_json(fd, 400, "Missing 'mode'");
        return;
    }
    mode = mode_item->valuestring;

    cfg = config_load(g_cfg.config_path);
    cc_set_charging_mode(mode, cfg);

    if (cfg) {
        cJSON *charging = cJSON_GetObjectItem(cfg, "charging");
        if (!charging) {
            charging = cJSON_CreateObject();
            cJSON_AddItemToObject(cfg, "charging", charging);
        }
        cJSON_DeleteItemFromObject(charging, "mode");
        cJSON_AddStringToObject(charging, "mode", mode);
        config_save(g_cfg.config_path, cfg);
        cJSON_Delete(cfg);
    }

    cJSON_Delete(req_json);
    send_json(fd, 200, "{\"success\":true}");
}

static void handle_post_charging_limit(int fd, const char *body)
{
    cJSON *req_json = cJSON_Parse(body);
    cJSON *limit_item = NULL;
    int limit = 0;

    if (!req_json) { send_error_json(fd, 400, "Invalid JSON"); return; }

    limit_item = cJSON_GetObjectItem(req_json, "limit");
    if (!cJSON_IsNumber(limit_item)) {
        cJSON_Delete(req_json);
        send_error_json(fd, 400, "Missing 'limit'");
        return;
    }
    limit = limit_item->valueint;
    cJSON_Delete(req_json);

    if (limit < 0 || limit > 100) { send_error_json(fd, 400, "limit must be 0-100"); return; }

    cc_set_charge_limit(limit);

    {
        cJSON *cfg = config_load(g_cfg.config_path);
        if (cfg) {
            cJSON *charging = cJSON_GetObjectItem(cfg, "charging");
            if (!charging) {
                charging = cJSON_CreateObject();
                cJSON_AddItemToObject(cfg, "charging", charging);
            }
            cJSON_DeleteItemFromObject(charging, "max_limit");
            cJSON_AddNumberToObject(charging, "max_limit", limit);
            config_save(g_cfg.config_path, cfg);
            cJSON_Delete(cfg);
        }
    }

    send_json(fd, 200, "{\"success\":true}");
}

static void handle_temperature_check(int fd)
{
    cJSON *cfg = config_load(g_cfg.config_path);
    float temp = 0.0f;
    const char *action = "none";
    float threshold = 40.0f;
    float critical  = 45.0f;
    cJSON *resp = NULL;
    char *json = NULL;

    if (cfg) {
        cJSON *charging = cJSON_GetObjectItem(cfg, "charging");
        if (charging) {
            cJSON *thr = cJSON_GetObjectItem(charging, "temperature_threshold");
            cJSON *cri = cJSON_GetObjectItem(charging, "temperature_critical");
            if (cJSON_IsNumber(thr)) threshold = (float)thr->valuedouble;
            if (cJSON_IsNumber(cri)) critical  = (float)cri->valuedouble;
        }
    }

    cc_check_temperature_protection(cfg, &temp, &action);

    resp = cJSON_CreateObject();
    cJSON_AddNumberToObject(resp, "temperature", (double)temp);
    cJSON_AddNumberToObject(resp, "threshold",   (double)threshold);
    cJSON_AddNumberToObject(resp, "critical",    (double)critical);
    cJSON_AddStringToObject(resp, "action",      action);

    json = cJSON_PrintUnformatted(resp);
    cJSON_Delete(resp);
    if (cfg) cJSON_Delete(cfg);

    send_json(fd, 200, json);
    free(json);
}

/* ------------------------------------------------------------------ */
/* Per-connection handler (runs in its own thread)                     */
/* ------------------------------------------------------------------ */

static void *handle_connection(void *arg)
{
    int fd = (int)(intptr_t)arg;
    http_request_t req;

    if (parse_request(fd, &req) != 0) {
        close(fd);
        return NULL;
    }

    /* OPTIONS preflight */
    if (strcmp(req.method, "OPTIONS") == 0) {
        send_response(fd, 204, "No Content", "text/plain", NULL, 0);
        close(fd);
        return NULL;
    }

    /* ---- API routes ---- */

    /* GET /api/battery */
    if (strcmp(req.method, "GET") == 0 && strcmp(req.path, "/api/battery") == 0) {
        handle_get_battery(fd);
    }
    /* GET /api/settings */
    else if (strcmp(req.method, "GET") == 0 && strcmp(req.path, "/api/settings") == 0) {
        handle_get_settings(fd);
    }
    /* POST /api/charging/enable */
    else if (strcmp(req.method, "POST") == 0 && strcmp(req.path, "/api/charging/enable") == 0) {
        handle_post_charging_enable(fd, req.body);
    }
    /* POST /api/charging/mode */
    else if (strcmp(req.method, "POST") == 0 && strcmp(req.path, "/api/charging/mode") == 0) {
        handle_post_charging_mode(fd, req.body);
    }
    /* POST /api/charging/limit */
    else if (strcmp(req.method, "POST") == 0 && strcmp(req.path, "/api/charging/limit") == 0) {
        handle_post_charging_limit(fd, req.body);
    }
    /* GET /api/charging/temperature-check */
    else if (strcmp(req.method, "GET") == 0 && strcmp(req.path, "/api/charging/temperature-check") == 0) {
        handle_temperature_check(fd);
    }
    /* GET /api/stats/daily */
    else if (strcmp(req.method, "GET") == 0 && strcmp(req.path, "/api/stats/daily") == 0) {
        char *json = NULL;
        if (stats_get_daily(7, &json) == 0) { send_json(fd, 200, json); free(json); }
        else send_error_json(fd, 500, "Database error");
    }
    /* GET /api/stats/weekly */
    else if (strcmp(req.method, "GET") == 0 && strcmp(req.path, "/api/stats/weekly") == 0) {
        char *json = NULL;
        if (stats_get_weekly(&json) == 0) { send_json(fd, 200, json); free(json); }
        else send_error_json(fd, 500, "Database error");
    }
    /* GET /api/stats/monthly */
    else if (strcmp(req.method, "GET") == 0 && strcmp(req.path, "/api/stats/monthly") == 0) {
        char *json = NULL;
        if (stats_get_monthly(&json) == 0) { send_json(fd, 200, json); free(json); }
        else send_error_json(fd, 500, "Database error");
    }
    /* GET /api/stats/snapshots */
    else if (strcmp(req.method, "GET") == 0 && strcmp(req.path, "/api/stats/snapshots") == 0) {
        char *json = NULL;
        if (stats_get_recent_snapshots(60, &json) == 0) { send_json(fd, 200, json); free(json); }
        else send_error_json(fd, 500, "Database error");
    }
    /* GET /api/stats/health */
    else if (strcmp(req.method, "GET") == 0 && strcmp(req.path, "/api/stats/health") == 0) {
        char *json = NULL;
        if (stats_get_health_trend(&json) == 0) { send_json(fd, 200, json); free(json); }
        else send_error_json(fd, 500, "Database error");
    }
    /* GET /api/export/csv */
    else if (strcmp(req.method, "GET") == 0 && strcmp(req.path, "/api/export/csv") == 0) {
        char *csv = NULL;
        if (stats_export_csv(&csv) == 0) {
            send_response(fd, 200, "OK", "text/csv; charset=utf-8", csv, strlen(csv));
            free(csv);
        } else send_error_json(fd, 500, "Database error");
    }
    /* GET /api/export/json */
    else if (strcmp(req.method, "GET") == 0 && strcmp(req.path, "/api/export/json") == 0) {
        char *json = NULL;
        if (stats_export_json(&json) == 0) { send_json(fd, 200, json); free(json); }
        else send_error_json(fd, 500, "Database error");
    }
    /* DELETE /api/stats/prune */
    else if (strcmp(req.method, "DELETE") == 0 && strcmp(req.path, "/api/stats/prune") == 0) {
        cJSON *cfg = config_load(g_cfg.config_path);
        int days = config_get_int(cfg, "stats", "retention_days", 90);
        int deleted = stats_prune_old_data(days);
        if (cfg) cJSON_Delete(cfg);
        char buf[64];
        snprintf(buf, sizeof(buf), "{\"deleted\":%d}", deleted);
        send_json(fd, 200, buf);
    }
    /* Static files */
    else if (strcmp(req.method, "GET") == 0) {
        serve_static(fd, req.path);
    }
    else {
        send_error_json(fd, 405, "Method Not Allowed");
    }

    close(fd);
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Server loop                                                         */
/* ------------------------------------------------------------------ */

int http_server_start(const http_server_config_t *cfg)
{
    struct sockaddr_in addr;
    int opt = 1;
    pthread_attr_t attr;

    if (!cfg) return -1;
    memcpy(&g_cfg, cfg, sizeof(g_cfg));

    /* Bug fix: SO_REUSEADDR prevents "Address already in use" on restart */
    g_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_server_fd < 0) return -1;

    setsockopt(g_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons((uint16_t)cfg->port);

    if (bind(g_server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(g_server_fd);
        g_server_fd = -1;
        return -1;
    }

    if (listen(g_server_fd, 32) < 0) {
        close(g_server_fd);
        g_server_fd = -1;
        return -1;
    }

    g_running = 1;

    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    /* Accept loop – each connection in a detached thread */
    while (g_running) {
        struct sockaddr_in client_addr;
        socklen_t clen = sizeof(client_addr);
        int client_fd;

        client_fd = accept(g_server_fd, (struct sockaddr *)&client_addr, &clen);
        if (client_fd < 0) {
            if (!g_running) break;
            continue;
        }

        /* Spawn detached handler thread */
        pthread_t handler;
        if (pthread_create(&handler, &attr, handle_connection, (void *)(intptr_t)client_fd) != 0) {
            close(client_fd);
        }
    }

    pthread_attr_destroy(&attr);
    close(g_server_fd);
    g_server_fd = -1;
    return 0;
}

void http_server_stop(void)
{
    g_running = 0;
    if (g_server_fd >= 0) {
        close(g_server_fd);
        g_server_fd = -1;
    }
}
