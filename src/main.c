/* ChargeControl – embedded HTTP server
 * main.c does not define _GNU_SOURCE directly; it is set via -D_GNU_SOURCE in CFLAGS.
 */
#include "charge_control.h"
#include "stats.h"
#include "snapshot_daemon.h"
#include "config.h"
#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <libgen.h>
#include <time.h>
#include <stdarg.h>

/* POSIX networking */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* ── global shutdown flag ────────────────────────────────── */
static volatile sig_atomic_t g_running = 1;

/* ── timestamped logging ─────────────────────────────────── */

static void cc_log(const char *fmt, ...)
    __attribute__((format(printf, 1, 2)));

static void cc_log(const char *fmt, ...)
{
    time_t now = time(NULL);
    struct tm *lt = localtime(&now);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", lt);
    fprintf(stdout, "[%s] ", ts);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    fflush(stdout);
}

static void handle_signal(int sig)
{
    (void)sig;
    g_running = 0;
    daemon_running = 0;
}

/* ── HTTP helpers ────────────────────────────────────────── */

#define RECV_BUF 8192

typedef struct {
    char method[8];
    char path[256];
    char body[4096];
    int  body_len;
} HttpRequest;

static void send_response(int fd, int status, const char *ctype,
                          const char *body, size_t blen)
{
    const char *reason = "OK";
    if      (status == 400) reason = "Bad Request";
    else if (status == 404) reason = "Not Found";
    else if (status == 405) reason = "Method Not Allowed";
    else if (status == 500) reason = "Internal Server Error";

    char header[512];
    int  hlen = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET,POST,DELETE,OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type\r\n"
        "Connection: close\r\n"
        "\r\n",
        status, reason, ctype, blen);

    send(fd, header, (size_t)hlen, MSG_NOSIGNAL);
    if (body && blen > 0)
        send(fd, body, blen, MSG_NOSIGNAL);
}

static void send_json(int fd, int status, const char *json)
{
    send_response(fd, status, "application/json", json, strlen(json));
}

static void send_json_free(int fd, int status, char *json)
{
    if (!json) {
        send_json(fd, 500, "{\"error\":\"internal error\"}");
        return;
    }
    send_json(fd, status, json);
    free(json);
}

static int parse_request(int fd, HttpRequest *req)
{
    char buf[RECV_BUF];
    int  n = (int)recv(fd, buf, sizeof(buf) - 1, 0);
    if (n <= 0) return -1;
    buf[n] = '\0';

    /* Method */
    char *sp1 = strchr(buf, ' ');
    if (!sp1) return -1;
    int mlen = (int)(sp1 - buf);
    if (mlen >= (int)sizeof(req->method)) return -1;
    memcpy(req->method, buf, (size_t)mlen);
    req->method[mlen] = '\0';

    /* Path */
    char *sp2 = strchr(sp1 + 1, ' ');
    if (!sp2) return -1;
    int plen = (int)(sp2 - (sp1 + 1));
    if (plen >= (int)sizeof(req->path)) plen = (int)sizeof(req->path) - 1;
    memcpy(req->path, sp1 + 1, (size_t)plen);
    req->path[plen] = '\0';

    /* Strip query string */
    char *qs = strchr(req->path, '?');
    if (qs) *qs = '\0';

    /* Body (after \r\n\r\n) */
    const char *body_start = strstr(buf, "\r\n\r\n");
    req->body_len = 0;
    req->body[0]  = '\0';
    if (body_start) {
        body_start += 4;
        int blen = n - (int)(body_start - buf);
        if (blen > (int)sizeof(req->body) - 1) blen = (int)sizeof(req->body) - 1;
        if (blen > 0) {
            memcpy(req->body, body_start, (size_t)blen);
            req->body[blen] = '\0';
            req->body_len   = blen;
        }
    }
    return 0;
}

/* ── route handlers ──────────────────────────────────────── */

static void handle_get_battery(int fd)
{
    BatteryStatus bs = cc_get_battery_status();
    char *json = cc_battery_status_to_json(&bs);
    send_json_free(fd, 200, json);
}

static void handle_get_settings(int fd)
{
    char *json = cc_get_all_settings_json();
    send_json_free(fd, 200, json);
}

static void handle_charging_enable(int fd, const char *body)
{
    cJSON *root = cJSON_Parse(body);
    if (!root) { send_json(fd, 400, "{\"error\":\"invalid JSON\"}"); return; }

    cJSON *en = cJSON_GetObjectItem(root, "enabled");
    int enabled = en ? (cJSON_IsTrue(en) ? 1 : 0) : -1;
    cJSON_Delete(root);

    if (enabled < 0) { send_json(fd, 400, "{\"error\":\"missing 'enabled'\"}"); return; }

    cc_set_charging_enabled(enabled);
    send_json(fd, 200, "{\"status\":\"ok\"}");
}

static void handle_charging_limit(int fd, const char *body)
{
    cJSON *root = cJSON_Parse(body);
    if (!root) { send_json(fd, 400, "{\"error\":\"invalid JSON\"}"); return; }

    cJSON *lim = cJSON_GetObjectItem(root, "limit");
    int limit = (lim && cJSON_IsNumber(lim)) ? (int)lim->valuedouble : -1;
    cJSON_Delete(root);

    if (cc_set_charge_limit(limit) != 0) {
        send_json(fd, 400, "{\"error\":\"limit must be 0-100\"}");
        return;
    }
    send_json(fd, 200, "{\"status\":\"ok\"}");
}

static void handle_charging_mode(int fd, const char *body)
{
    cJSON *root = cJSON_Parse(body);
    if (!root) { send_json(fd, 400, "{\"error\":\"invalid JSON\"}"); return; }

    cJSON *mo = cJSON_GetObjectItem(root, "mode");
    char mode[MODE_NAME_LEN] = "";
    if (mo && cJSON_IsString(mo) && mo->valuestring)
        snprintf(mode, sizeof(mode), "%s", mo->valuestring);
    cJSON_Delete(root);

    if (cc_set_charging_mode(mode) != 0) {
        send_json(fd, 400, "{\"error\":\"unknown mode\"}");
        return;
    }
    send_json(fd, 200, "{\"status\":\"ok\"}");
}

static void handle_temperature_check(int fd)
{
    TempProtectionResult r = cc_check_temperature_protection();
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(obj, "temperature", r.temperature);
    cJSON_AddNumberToObject(obj, "threshold",   r.threshold);
    cJSON_AddNumberToObject(obj, "critical",    r.critical);
    cJSON_AddStringToObject(obj, "action",      r.action);
    char *s = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    send_json_free(fd, 200, s);
}

static void handle_stats(int fd, const char *period)
{
    char *json = NULL;
    if      (strcmp(period, "daily")   == 0) json = stats_get_daily_stats(7);
    else if (strcmp(period, "weekly")  == 0) json = stats_get_weekly_stats();
    else if (strcmp(period, "monthly") == 0) json = stats_get_monthly_stats();
    else if (strcmp(period, "snapshots") == 0) json = stats_get_recent_snapshots(60);
    else if (strcmp(period, "health")  == 0) json = stats_get_battery_health();
    else { send_json(fd, 404, "{\"error\":\"unknown stats endpoint\"}"); return; }
    send_json_free(fd, 200, json);
}

static void handle_export_csv(int fd)
{
    char *csv = stats_export_csv();
    if (!csv) { send_json(fd, 500, "{\"error\":\"export failed\"}"); return; }

    char ts[32];
    time_t now = time(NULL);
    struct tm *lt = localtime(&now);
    strftime(ts, sizeof(ts), "%Y-%m-%d_%H%M%S", lt);

    char fname[64];
    snprintf(fname, sizeof(fname), "charging_data_%s.csv", ts);

    char header[320];
    int hlen = snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/csv\r\n"
        "Content-Disposition: attachment; filename=\"%s\"\r\n"
        "Content-Length: %zu\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Connection: close\r\n"
        "\r\n",
        fname, strlen(csv));
    send(fd, header, (size_t)hlen, MSG_NOSIGNAL);
    send(fd, csv, strlen(csv), MSG_NOSIGNAL);
    free(csv);
}

static void handle_export_json(int fd)
{
    char *json = stats_export_json();
    if (!json) { send_json(fd, 500, "{\"error\":\"export failed\"}"); return; }

    char ts[32];
    time_t now = time(NULL);
    struct tm *lt = localtime(&now);
    strftime(ts, sizeof(ts), "%Y-%m-%d_%H%M%S", lt);

    char fname[64];
    snprintf(fname, sizeof(fname), "charging_data_%s.json", ts);

    char header[320];
    int hlen = snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Content-Disposition: attachment; filename=\"%s\"\r\n"
        "Content-Length: %zu\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Connection: close\r\n"
        "\r\n",
        fname, strlen(json));
    send(fd, header, (size_t)hlen, MSG_NOSIGNAL);
    send(fd, json, strlen(json), MSG_NOSIGNAL);
    free(json);
}

static void handle_post_config(int fd, const char *body)
{
    cJSON *root = cJSON_Parse(body);
    if (!root) { send_json(fd, 400, "{\"error\":\"invalid JSON\"}"); return; }

    /* Write the received JSON directly to config.json */
    char *formatted = cJSON_Print(root);
    cJSON_Delete(root);
    if (!formatted) { send_json(fd, 500, "{\"error\":\"serialise failed\"}"); return; }

    FILE *fp = fopen(cc_config_path(), "w");
    if (!fp) { free(formatted); send_json(fd, 500, "{\"error\":\"write failed\"}"); return; }
    fputs(formatted, fp);
    fclose(fp);
    free(formatted);
    send_json(fd, 200, "{\"status\":\"ok\"}");
}

static void handle_prune(int fd, const char *body)
{
    int days = 90;
    cJSON *root = cJSON_Parse(body);
    if (root) {
        cJSON *d = cJSON_GetObjectItem(root, "retention_days");
        if (d && cJSON_IsNumber(d)) days = (int)d->valuedouble;
        cJSON_Delete(root);
    }

    int deleted = stats_prune_old_data(days);
    char resp[64];
    snprintf(resp, sizeof(resp), "{\"deleted\":%d}", deleted);
    send_json(fd, 200, resp);
}

/* ── main dispatch ───────────────────────────────────────── */

static void dispatch(int fd, const HttpRequest *req)
{
    const char *p = req->path;
    const char *m = req->method;

    /* Preflight OPTIONS */
    if (strcmp(m, "OPTIONS") == 0) {
        send_json(fd, 200, "{}");
        return;
    }

    if (strcmp(p, "/api/battery") == 0 && strcmp(m, "GET") == 0) {
        handle_get_battery(fd);
    } else if (strcmp(p, "/api/settings") == 0 && strcmp(m, "GET") == 0) {
        handle_get_settings(fd);
    } else if (strcmp(p, "/api/charging/enable") == 0 && strcmp(m, "POST") == 0) {
        handle_charging_enable(fd, req->body);
    } else if (strcmp(p, "/api/charging/limit") == 0 && strcmp(m, "POST") == 0) {
        handle_charging_limit(fd, req->body);
    } else if (strcmp(p, "/api/charging/mode") == 0 && strcmp(m, "POST") == 0) {
        handle_charging_mode(fd, req->body);
    } else if (strcmp(p, "/api/charging/temperature-check") == 0 && strcmp(m, "POST") == 0) {
        handle_temperature_check(fd);
    } else if (strcmp(p, "/api/stats/daily") == 0 && strcmp(m, "GET") == 0) {
        handle_stats(fd, "daily");
    } else if (strcmp(p, "/api/stats/weekly") == 0 && strcmp(m, "GET") == 0) {
        handle_stats(fd, "weekly");
    } else if (strcmp(p, "/api/stats/monthly") == 0 && strcmp(m, "GET") == 0) {
        handle_stats(fd, "monthly");
    } else if (strcmp(p, "/api/stats/snapshots") == 0 && strcmp(m, "GET") == 0) {
        handle_stats(fd, "snapshots");
    } else if (strcmp(p, "/api/stats/health") == 0 && strcmp(m, "GET") == 0) {
        handle_stats(fd, "health");
    } else if (strcmp(p, "/api/export/csv") == 0 && strcmp(m, "GET") == 0) {
        handle_export_csv(fd);
    } else if (strcmp(p, "/api/export/json") == 0 && strcmp(m, "GET") == 0) {
        handle_export_json(fd);
    } else if (strcmp(p, "/api/config") == 0 && strcmp(m, "POST") == 0) {
        handle_post_config(fd, req->body);
    } else if (strcmp(p, "/api/stats/prune") == 0 && strcmp(m, "DELETE") == 0) {
        handle_prune(fd, req->body);
    } else {
        send_json(fd, 404, "{\"error\":\"not found\"}");
    }
}

/* ── determine MODDIR ────────────────────────────────────── */

static void resolve_moddir(const char *argv0, char *out, size_t outsz)
{
    /* Try /proc/self/exe first */
    char exe[512];
    ssize_t r = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (r > 0) {
        exe[r] = '\0';
        char *d = dirname(exe);
        snprintf(out, outsz, "%s", d);
        return;
    }
    /* Fall back to argv[0] */
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", argv0);
    char *d = dirname(tmp);
    snprintf(out, outsz, "%s", d);
}

/* ── entry point ─────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    /* Determine module directory */
    char moddir[512];
    if (argc >= 2 && argv[1][0] == '/') {
        snprintf(moddir, sizeof(moddir), "%s", argv[1]);
    } else {
        resolve_moddir(argv[0], moddir, sizeof(moddir));
    }

    /* Initialise subsystems */
    cc_init(moddir);

    /* Load config for host/port */
    ChargeConfig cfg;
    cc_load_config(&cfg);

    /* Initialise database */
    if (stats_init_db(cc_db_path()) != 0) {
        cc_log("WARNING: could not initialise database at %s\n",
               cc_db_path());
    }

    /* Start snapshot daemon thread */
    if (snapshot_daemon_start() != 0) {
        cc_log("WARNING: could not start snapshot daemon\n");
    }

    /* Signal handling */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT,  &sa, NULL);
    signal(SIGPIPE, SIG_IGN);

    /* Create listening socket */
    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) { perror("socket"); return 1; }

    int one = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons((uint16_t)cfg.server_port);

    if (strcmp(cfg.server_host, "0.0.0.0") == 0 ||
        strcmp(cfg.server_host, "") == 0) {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        inet_pton(AF_INET, cfg.server_host, &addr.sin_addr);
    }

    if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); close(srv); return 1;
    }
    if (listen(srv, 16) < 0) {
        perror("listen"); close(srv); return 1;
    }

    cc_log("ChargeControl HTTP server listening on %s:%d\n",
            cfg.server_host, cfg.server_port);

    /* Accept loop */
    while (g_running) {
        struct sockaddr_in client_addr;
        socklen_t clen = sizeof(client_addr);
        int cfd = accept(srv, (struct sockaddr *)&client_addr, &clen);
        if (cfd < 0) {
            if (errno == EINTR) break;
            continue;
        }

        HttpRequest req;
        memset(&req, 0, sizeof(req));
        if (parse_request(cfd, &req) == 0)
            dispatch(cfd, &req);

        close(cfd);
    }

    close(srv);
    snapshot_daemon_stop();
    cc_log("ChargeControl stopped.\n");
    return 0;
}
