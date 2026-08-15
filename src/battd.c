#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/select.h>

#define PORT        8800
#define SYSFS       "/sys/class/power_supply/battery"
#define SYSFS_USB   "/sys/class/power_supply/usb"
#define THERMAL     "/sys/class/thermal"
#define CONFIG      "/data/adb/battery-manager/batt.conf"
#define HISTORY     "/data/adb/battery-manager/history.json"
#define LOGFILE     "/data/adb/battery-manager/battd.log"
#define WEBROOT     "/data/adb/battery-manager/webroot"
#define MAX_LINE    4096
#define HIST_MAX    500
#define FULL_TIMEOUT 1800

static volatile int running = 1;
static int charge_limit = 80;
static int temp_limit = 45;
static int resume_delta = 5;
static int interval = 5;
static int limit_enabled = 1;
static int temp_suspended = 0;
static time_t last_save = 0;
static int full_once = 0;
static time_t full_until = 0;

static int bat_capacity = -1, bat_temp = -1, bat_volt = -1, bat_curr = -1;
static char bat_status[32] = "Unknown";
static int soc_temp = -1, gpu_temp = -1, chg_temp = -1, case_temp = -1;
static int cycle_count = -1, health = -1;
static int usb_online = 0, usb_power_max = 0, pd_type = 0;
static char proto_name[32] = "未知";
static char control_state[24] = "idle";

static int read_int(const char *path) {
    char buf[64]; int fd = open(path, O_RDONLY);
    if (fd < 0) return -1; int n = read(fd, buf, sizeof(buf)-1);
    close(fd); if (n <= 0) return -1; buf[n] = '\0'; return atoi(buf);
}
static int read_str(const char *path, char *out, int size) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1; int n = read(fd, out, size-1);
    close(fd); if (n <= 0) return -1; out[n] = '\0';
    while (n > 0 && (out[n-1]=='\n'||out[n-1]=='\r')) out[--n] = '\0';
    return n;
}
static int write_str(const char *path, const char *val) {
    int fd = open(path, O_WRONLY); if (fd < 0) return -1;
    int n = write(fd, val, strlen(val)); close(fd); return n;
}

static void log_event(const char *msg) {
    struct stat st;
    if (stat(LOGFILE, &st) == 0 && st.st_size > 256 * 1024)
        rename(LOGFILE, "/data/adb/battery-manager/battd.log.old");
    FILE *f = fopen(LOGFILE, "a");
    if (!f) return;
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    fprintf(f, "[%04d-%02d-%02d %02d:%02d:%02d] %s\n",
            tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday,
            tm->tm_hour, tm->tm_min, tm->tm_sec, msg);
    fclose(f);
}

static void update_battery(void) {
    char path[MAX_LINE]; int v;
    snprintf(path, sizeof(path), "%s/capacity", SYSFS);
    if ((v = read_int(path)) >= 0) bat_capacity = v;
    snprintf(path, sizeof(path), "%s/temp", SYSFS);
    if ((v = read_int(path)) >= 0) bat_temp = v / 10;
    snprintf(path, sizeof(path), "%s/voltage_now", SYSFS);
    if ((v = read_int(path)) >= 0) bat_volt = v / 1000;
    snprintf(path, sizeof(path), "%s/current_now", SYSFS);
    if ((v = read_int(path)) >= 0) bat_curr = v / 1000;
    snprintf(path, sizeof(path), "%s/status", SYSFS);
    read_str(path, bat_status, sizeof(bat_status));

    soc_temp = -1; gpu_temp = -1; chg_temp = -1; case_temp = -1;
    for (int i = 0; i < 100; i++) {
        char tzpath[128]; snprintf(tzpath, sizeof(tzpath), "%s/thermal_zone%d/type", THERMAL, i);
        char tname[64];
        if (read_str(tzpath, tname, sizeof(tname)) <= 0) break;
        snprintf(tzpath, sizeof(tzpath), "%s/thermal_zone%d/temp", THERMAL, i);
        int tval = read_int(tzpath);
        if (tval < 0) continue;
        if (!strcmp(tname, "soc_max")) soc_temp = tval / 1000;
        else if (!strcmp(tname, "gpu1")) gpu_temp = tval / 1000;
        else if (!strcmp(tname, "mtk-master-charger")) chg_temp = tval / 1000;
        else if (!strcmp(tname, "X7_therm") || !strcmp(tname, "charger2_therm")) {
            if (case_temp < 0) case_temp = tval / 1000;
        }
    }

    snprintf(path, sizeof(path), "%s/cycle_count", SYSFS);
    if ((v = read_int(path)) >= 0) cycle_count = v;
    int full = 0, design = 0;
    snprintf(path, sizeof(path), "%s/charge_full", SYSFS);
    if ((v = read_int(path)) >= 0) full = v;
    snprintf(path, sizeof(path), "%s/charge_full_design", SYSFS);
    if ((v = read_int(path)) >= 0) design = v;
    if (design > 0) health = (full * 100) / design;

    usb_online = read_int(SYSFS_USB "/online");
    usb_power_max = read_int(SYSFS_USB "/power_max");
    pd_type = read_int(SYSFS_USB "/pd_type");
    char buf[64];
    if (read_str(SYSFS_USB "/real_type", buf, sizeof(buf)) > 0)
        strncpy(proto_name, buf, sizeof(proto_name)-1);
    else if (read_str(SYSFS_USB "/type", buf, sizeof(buf)) > 0)
        strncpy(proto_name, buf, sizeof(proto_name)-1);
    else strcpy(proto_name, "未知");

    if (!limit_enabled) strcpy(control_state, "disabled");
    else if (temp_suspended) strcpy(control_state, "temp_protect");
    else if (full_once) strcpy(control_state, "manual_full");
    else if (bat_capacity >= charge_limit && strcmp(bat_status, "Charging") == 0) strcpy(control_state, "suspended");
    else if (strcmp(bat_status, "Charging") == 0) strcpy(control_state, "charging");
    else strcpy(control_state, "idle");
}

static void apply_control(void) {
    char path[MAX_LINE];
    snprintf(path, sizeof(path), "%s/input_suspend", SYSFS);

    if (full_once) {
        if (time(NULL) >= full_until) {
            full_once = 0;
            log_event("手动充满结束，恢复充电限制");
        } else {
            write_str(path, "0");
            return;
        }
    }
    if (!limit_enabled) {
        write_str(path, "0"); temp_suspended = 0; return;
    }
    if (bat_temp >= temp_limit) {
        write_str(path, "1"); temp_suspended = 1; return;
    }
    if (temp_suspended) {
        write_str(path, "0"); temp_suspended = 0;
    }
    if (bat_capacity >= charge_limit) {
        write_str(path, "1");
    } else if (bat_capacity <= charge_limit - resume_delta) {
        write_str(path, "0");
    }
}
static void load_config(void) {
    FILE *f = fopen(CONFIG, "r"); if (!f) return;
    char line[MAX_LINE];
    while (fgets(line, sizeof(line), f)) {
        if (line[0]=='#'||line[0]=='\n') continue;
        char *p = strchr(line, '\n'); if (p) *p = '\0';
        char *eq = strchr(line, '='); if (!eq) continue;
        *eq = '\0';
        const char *k = line, *v = eq + 1;
        if (!strcmp(k, "charge_limit")) charge_limit = atoi(v);
        else if (!strcmp(k, "temp_limit")) temp_limit = atoi(v);
        else if (!strcmp(k, "resume_delta")) resume_delta = atoi(v);
        else if (!strcmp(k, "interval")) interval = atoi(v);
        else if (!strcmp(k, "enabled")) limit_enabled = atoi(v);
    }
    fclose(f);
}
static void save_config(void) {
    FILE *f = fopen(CONFIG, "w"); if (!f) return;
    fprintf(f, "charge_limit=%d\n", charge_limit);
    fprintf(f, "temp_limit=%d\n", temp_limit);
    fprintf(f, "resume_delta=%d\n", resume_delta);
    fprintf(f, "interval=%d\n", interval);
    fprintf(f, "enabled=%d\n", limit_enabled);
    fclose(f);
}

static void push_history(void) {
    time_t now = time(NULL);
    if (now - last_save < 60) return;
    struct stat st;
    if (stat(HISTORY, &st) == 0 && st.st_size > 512 * 1024) {
        FILE *f = fopen(HISTORY, "r");
        if (f) {
            fseek(f, -256 * 1024, SEEK_END);
            long pos = ftell(f);
            if (pos < 0) { fclose(f); return; }
            long tail_len = st.st_size - pos;
            char *tail = malloc(tail_len + 1);
            if (!tail) { fclose(f); return; }
            size_t _fr = fread(tail, 1, tail_len, f); (void)_fr;
            fclose(f); tail[tail_len] = '\0';
            char *p = tail; while (*p && *p != '\n') p++;
            if (*p == '\n') p++;
            f = fopen(HISTORY, "w");
            if (f) { fwrite(p, 1, strlen(p), f); fclose(f); }
            free(tail);
        }
    }
    FILE *f = fopen(HISTORY, "a"); if (!f) return;
    fprintf(f, "{\"t\":%ld,\"c\":%d,\"tmp\":%d,\"v\":%d,\"s\":\"%s\"}\n",
            (long)now, bat_capacity, bat_temp, bat_volt, bat_status);
    fclose(f);
    last_save = now;
}

static char *history_json(void) {
    FILE *f = fopen(HISTORY, "r"); if (!f) return strdup("[]");
    fseek(f, 0, SEEK_END); long sz = ftell(f); rewind(f);
    if (sz <= 0 || sz > (1<<20)) { fclose(f); return strdup("[]"); }
    char *buf = malloc(sz + 1); size_t rd = fread(buf, 1, sz, f); fclose(f);
    buf[rd] = '\0';
    int nl = 0; for (char *s = buf; *s; s++) if (*s == '\n') nl++;
    int keep = nl > HIST_MAX ? nl - HIST_MAX : 0;
    char *start = buf; int seen = 0;
    for (char *s = buf; *s && seen < keep; s++) if (*s == '\n') { start = s+1; seen++; }
    for (char *p = start; *p; p++) if (*p == '\n') *p = ',';
    size_t blen = strlen(start);
    if (blen > 0 && start[blen-1] == ',') start[blen-1] = '\0', blen--;
    char *out = malloc(blen + 3); out[0] = '[';
    memcpy(out+1, start, blen); out[blen+1] = ']'; out[blen+2] = '\0';
    free(buf); return out;
}

static void send_resp(int fd, int code, const char *ctype, const char *body) {
    char head[512];
    int n = snprintf(head, sizeof(head),
        "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
        "Connection: close\r\nAccess-Control-Allow-Origin: *\r\n\r\n",
        code, code==200?"OK":"Not Found", ctype, body?strlen(body):0);
    ssize_t _w = write(fd, head, n); (void)_w;
    if (body) { ssize_t _w2 = write(fd, body, strlen(body)); (void)_w2; }
}

static void handle_status(int fd) {
    char body[2048];
    int charge_full = read_int(SYSFS "/charge_full") / 1000;
    int charge_power = 0;
    if (bat_volt > 0 && bat_curr < 0)
        charge_power = (bat_volt * (-bat_curr)) / 1000;
    int est_full_min = 0;
    if (charge_power > 0 && charge_limit > bat_capacity && charge_full > 0) {
        int remain = ((charge_limit - bat_capacity) * charge_full) / 100;
        est_full_min = (remain * 60) / (-bat_curr > 0 ? -bat_curr : 1);
    }
    const char *health_rating = "未知";
    if (health >= 90) health_rating = "优秀";
    else if (health >= 80) health_rating = "良好";
    else if (health >= 70) health_rating = "一般";
    else if (health >= 0) health_rating = "较差";
    int est_cycles_left = 0;
    if (cycle_count > 0 && health > 0) {
        float loss_per_cycle = (100.0f - health) / cycle_count;
        if (loss_per_cycle > 0.001f)
            est_cycles_left = (int)((health - 60.0f) / loss_per_cycle);
    }
    snprintf(body, sizeof(body),
        "{\"capacity\":%d,\"temp\":%d,\"voltage\":%d,\"current\":%d,"
        "\"status\":\"%s\",\"limit_enabled\":%d,\"charge_limit\":%d,"
        "\"temp_limit\":%d,\"resume_delta\":%d,\"interval\":%d,"
        "\"soc_temp\":%d,\"gpu_temp\":%d,\"chg_temp\":%d,\"case_temp\":%d,"
        "\"cycle_count\":%d,\"health\":%d,\"health_rating\":\"%s\","
        "\"charge_full_mah\":%d,\"est_cycles_left\":%d,"
        "\"charge_power_w\":%d,\"est_full_min\":%d,"
        "\"usb_online\":%d,\"proto_name\":\"%s\",\"pd_type\":%d,\"power_max\":%d,"
        "\"control_state\":\"%s\",\"full_once\":%d,\"full_until\":%ld}",
        bat_capacity, bat_temp, bat_volt, bat_curr, bat_status,
        limit_enabled, charge_limit, temp_limit, resume_delta, interval,
        soc_temp, gpu_temp, chg_temp, case_temp,
        cycle_count, health, health_rating,
        charge_full, est_cycles_left,
        charge_power, est_full_min,
        usb_online, proto_name, pd_type, usb_power_max,
        control_state, full_once, (long)full_until);
    send_resp(fd, 200, "application/json", body);
}

static void handle_limit(int fd, const char *body) {
    if (body) {
        char *copy = strdup(body); char *save = NULL;
        for (char *tok = strtok_r(copy, "&", &save); tok; tok = strtok_r(NULL, "&", &save)) {
            char *eq = strchr(tok, '='); if (!eq) continue;
            *eq = '\0'; char key[64], val[64];
            strncpy(key, tok, 63); key[63] = '\0';
            strncpy(val, eq+1, 63); val[63] = '\0';
            const char *dec = val;
            if (!strcmp(key, "charge_limit")) charge_limit = atoi(dec);
            else if (!strcmp(key, "temp_limit")) temp_limit = atoi(dec);
            else if (!strcmp(key, "resume_delta")) resume_delta = atoi(dec);
            else if (!strcmp(key, "interval")) interval = atoi(dec);
            else if (!strcmp(key, "enabled")) limit_enabled = atoi(dec);
        }
        free(copy);
        save_config(); apply_control();
        log_event("配置已更新");
    }
    send_resp(fd, 200, "application/json", "{\"ok\":true}");
}

static void handle_full(int fd) {
    full_once = 1; full_until = time(NULL) + FULL_TIMEOUT;
    log_event("手动充满已启动，30分钟超时");
    send_resp(fd, 200, "application/json", "{\"ok\":true,\"timeout\":1800}");
}

static void handle_export(int fd) {
    char buf[8192]; int n = 0;
    n += snprintf(buf+n, sizeof(buf)-n, "=== ChargeControl 导出 ===\n时间: %ld\n\n--- 配置 ---\n", (long)time(NULL));
    FILE *f = fopen(CONFIG, "r");
    if (f) { while (n < (int)sizeof(buf)-256 && fgets(buf+n, sizeof(buf)-n, f)) n += strlen(buf+n); fclose(f); }
    n += snprintf(buf+n, sizeof(buf)-n, "\n--- 日志 ---\n");
    f = fopen(LOGFILE, "r");
    if (f) { while (n < (int)sizeof(buf)-256 && fgets(buf+n, sizeof(buf)-n, f)) n += strlen(buf+n); fclose(f); }
    n += snprintf(buf+n, sizeof(buf)-n, "\n--- 历史 (最后20条) ---\n");
    f = fopen(HISTORY, "r");
    if (f) {
        char *lines[20]; int lc = 0;
        char line[512];
        while (fgets(line, sizeof(line), f) && lc < 20) {
            lines[lc] = strdup(line); lc++;
        }
        fclose(f);
        for (int i = 0; i < lc; i++) {
            n += snprintf(buf+n, sizeof(buf)-n, "%s", lines[i]); free(lines[i]);
        }
    }
    char head[512];
    int hn = snprintf(head, sizeof(head),
        "HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\n"
        "Content-Disposition: attachment; filename=\"chargecontrol_export.txt\"\r\n"
        "Content-Length: %d\r\nConnection: close\r\nAccess-Control-Allow-Origin: *\r\n\r\n", n);
    ssize_t _w = write(fd, head, hn); (void)_w;
    ssize_t _we = write(fd, buf, n); (void)_we;
}

static int serve_file(int fd, const char *uri) {
    char path[512];
    if (!strcmp(uri, "/") || !strcmp(uri, "/index.html"))
        snprintf(path, sizeof(path), "%s/index.html", WEBROOT);
    else snprintf(path, sizeof(path), "%s%s", WEBROOT, uri);
    if (strstr(path, "..")) { send_resp(fd, 404, "text/plain", "Not Found"); return -1; }
    FILE *f = fopen(path, "rb"); if (!f) { send_resp(fd, 404, "text/plain", "Not Found"); return -1; }
    fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
    char *body = malloc(len+1);
    if (!body) { fclose(f); send_resp(fd, 500, "text/plain", "OOM"); return -1; }
    size_t rd = fread(body, 1, len, f); fclose(f);
    body[rd] = '\0';
    const char *ct = "text/plain";
    if (strstr(uri, ".html")) ct = "text/html";
    else if (strstr(uri, ".css")) ct = "text/css";
    else if (strstr(uri, ".js")) ct = "application/javascript";
    else if (strstr(uri, ".svg")) ct = "image/svg+xml";
    char head[512];
    int n = snprintf(head, sizeof(head),
        "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\nAccess-Control-Allow-Origin: *\r\n\r\n", ct, rd);
    ssize_t _w = write(fd, head, n); (void)_w;
    ssize_t _w3 = write(fd, body, rd); (void)_w3;
    free(body); return 0;
}

static void handle_client(int fd) {
    char buf[MAX_LINE]; int n = read(fd, buf, sizeof(buf)-1);
    if (n <= 0) return; buf[n] = '\0';
    char method[16], uri[256];
    if (sscanf(buf, "%15s %255s", method, uri) != 2) return;
    char *body = NULL;
    if (!strcmp(method, "POST")) {
        char *hp = strstr(buf, "\r\n\r\n");
        if (hp) body = hp + 4;
    }
    if (!strcmp(uri, "/api/status")) { handle_status(fd); return; }
    if (!strcmp(uri, "/api/limit") && !strcmp(method, "POST")) { handle_limit(fd, body); return; }
    if (!strcmp(uri, "/api/full") && !strcmp(method, "POST")) { handle_full(fd); return; }
    if (!strcmp(uri, "/api/log")) {
        FILE *lf = fopen(LOGFILE, "r");
        if (!lf) { send_resp(fd, 200, "text/plain", "暂无日志"); return; }
        fseek(lf, 0, SEEK_END); long lsz = ftell(lf); rewind(lf);
        if (lsz > 0 && lsz < 65536) {
            char *lbuf = malloc(lsz+1); size_t lrd = fread(lbuf, 1, lsz, lf); fclose(lf);
            lbuf[lrd] = '\0'; send_resp(fd, 200, "text/plain", lbuf); free(lbuf);
        } else { fclose(lf); send_resp(fd, 200, "text/plain", "日志过大"); }
        return;
    }
    if (!strcmp(uri, "/api/export")) { handle_export(fd); return; }
    if (!strcmp(uri, "/api/history")) { char *j = history_json(); send_resp(fd, 200, "application/json", j); free(j); return; }
    if (!strcmp(method, "GET")) serve_file(fd, uri);
    else send_resp(fd, 405, "text/plain", "Method Not Allowed");
}

static void on_sig(int sig) { (void)sig; running = 0; }

int main(void) {
    load_config(); last_save = time(NULL);
    update_battery();
    log_event("ChargeControl 守护进程启动");

    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) return 1;
    int opt = 1; setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr; memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET; addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(PORT);
    if (bind(srv, (struct sockaddr*)&addr, sizeof(addr)) < 0) return 1;
    if (listen(srv, 8) < 0) return 1;
    signal(SIGINT, on_sig); signal(SIGTERM, on_sig);

    time_t last_poll = 0;
    while (running) {
        fd_set rfds; FD_ZERO(&rfds); FD_SET(srv, &rfds);
        struct timeval tv = { 1, 0 };
        int s = select(srv+1, &rfds, NULL, NULL, &tv);
        if (s < 0) { sleep(1); continue; }
        if (s > 0 && FD_ISSET(srv, &rfds)) {
            struct sockaddr_in cli; socklen_t cli_len = sizeof(cli);
            int cfd = accept(srv, (struct sockaddr*)&cli, &cli_len);
            if (cfd >= 0) { handle_client(cfd); close(cfd); }
        }
        time_t now = time(NULL);
        if (now - last_poll >= (time_t)interval) {
            last_poll = now; update_battery(); apply_control(); push_history();
        }
    }
    log_event("ChargeControl 守护进程停止");
    close(srv); return 0;
}
