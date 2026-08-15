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
#define CONFIG      "/data/adb/battery-manager/batt.conf"
#define HISTORY     "/data/adb/battery-manager/history.json"
#define WEBROOT     "/data/adb/battery-manager/webroot"
#define MAX_LINE    4096
#define HIST_MAX    500

static volatile int running = 1;
static int charge_limit = 80;     /* 停止充电阈值 % */
static int temp_limit = 45;       /* 温度上限 °C */
static int resume_delta = 5;      /* 低于上限 resume_delta% 恢复充电 */
static int interval = 5;          /* 轮询间隔秒 */
static int limit_enabled = 1;
static int temp_suspended = 0;
static time_t last_save = 0;

static int bat_capacity = -1;
static int bat_temp = -1;
static int bat_volt = -1;
static int bat_curr = -1;
static char bat_status[32] = "Unknown";

/* ---------- sysfs helper ---------- */
static int read_int(const char *path) {
    char buf[64];
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    int n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return -1;
    buf[n] = '\0';
    return atoi(buf);
}

static int read_str(const char *path, char *out, int size) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    int n = read(fd, out, size - 1);
    close(fd);
    if (n <= 0) return -1;
    out[n] = '\0';
    while (n > 0 && (out[n-1] == '\n' || out[n-1] == '\r')) out[--n] = '\0';
    return n;
}

static int write_str(const char *path, const char *val) {
    int fd = open(path, O_WRONLY);
    if (fd < 0) return -1;
    int n = write(fd, val, strlen(val));
    close(fd);
    return n;
}

/* ---------- battery ---------- */
static void update_battery(void) {
    char path[MAX_LINE];
    int v;
    snprintf(path, sizeof(path), "%s/capacity", SYSFS);
    if ((v = read_int(path)) >= 0) bat_capacity = v;
    snprintf(path, sizeof(path), "%s/temp", SYSFS);
    if ((v = read_int(path)) >= 0) bat_temp = v / 10;   /* 323 -> 32.3 -> 32 */
    snprintf(path, sizeof(path), "%s/voltage_now", SYSFS);
    if ((v = read_int(path)) >= 0) bat_volt = v / 1000; /* uV -> mV */
    snprintf(path, sizeof(path), "%s/current_now", SYSFS);
    if ((v = read_int(path)) >= 0) bat_curr = v / 1000; /* uA -> mA */
    snprintf(path, sizeof(path), "%s/status", SYSFS);
    read_str(path, bat_status, sizeof(bat_status));
}

static void apply_control(void) {
    char path[MAX_LINE];
    snprintf(path, sizeof(path), "%s/input_suspend", SYSFS);

    if (!limit_enabled) {
        write_str(path, "0");
        temp_suspended = 0;
        return;
    }
    /* 温度保护：超限则暂停，恢复后取消暂停标志 */
    if (bat_temp >= temp_limit) {
        write_str(path, "1");
        temp_suspended = 1;
        return;
    }
    if (temp_suspended) {
        /* 温度已恢复，解除暂停 */
        write_str(path, "0");
        temp_suspended = 0;
    }
    if (bat_capacity >= charge_limit) {
        write_str(path, "1");
    } else if (bat_capacity <= charge_limit - resume_delta) {
        write_str(path, "0");
    }
}

/* ---------- config ---------- */
static void load_config(void) {
    FILE *f = fopen(CONFIG, "r");
    if (!f) return;
    char line[MAX_LINE];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char *p = strchr(line, '\n'); if (p) *p = '\0';
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        const char *k = line; const char *v = eq + 1;
        if      (!strcmp(k, "charge_limit")) charge_limit = atoi(v);
        else if (!strcmp(k, "temp_limit"))   temp_limit = atoi(v);
        else if (!strcmp(k, "resume_delta")) resume_delta = atoi(v);
        else if (!strcmp(k, "interval"))     interval = atoi(v);
        else if (!strcmp(k, "enabled"))      limit_enabled = atoi(v);
    }
    fclose(f);
}

static void save_config(void) {
    FILE *f = fopen(CONFIG, "w");
    if (!f) return;
    fprintf(f, "charge_limit=%d\n", charge_limit);
    fprintf(f, "temp_limit=%d\n", temp_limit);
    fprintf(f, "resume_delta=%d\n", resume_delta);
    fprintf(f, "interval=%d\n", interval);
    fprintf(f, "enabled=%d\n", limit_enabled);
    fclose(f);
}

/* ---------- history ---------- */
static void push_history(void) {
    time_t now = time(NULL);
    /* 每 60 秒记录一次 */
    if (now - last_save < 60) return;

    /* 文件超过 512KB 时截断只保留尾部 256KB */
    struct stat st;
    if (stat(HISTORY, &st) == 0 && st.st_size > 512 * 1024) {
        FILE *f = fopen(HISTORY, "r");
        if (f) {
            long keep = 256 * 1024;
            fseek(f, -keep, SEEK_END);
            long pos = ftell(f);
            if (pos < 0) { fclose(f); return; }
            long tail_len = st.st_size - pos;
            char *tail = malloc(tail_len + 1);
            if (!tail) { fclose(f); return; }
            size_t _fr = fread(tail, 1, tail_len, f); (void)_fr;
            fclose(f);
            tail[tail_len] = '\0';
            /* 跳到第一个完整行 */
            char *p = tail;
            while (*p && *p != '\n') p++;
            if (*p == '\n') p++;
            f = fopen(HISTORY, "w");
            if (f) { fwrite(p, 1, strlen(p), f); fclose(f); }
            free(tail);
        }
    }

    FILE *f = fopen(HISTORY, "a");
    if (!f) return;
    fprintf(f, "{\"t\":%ld,\"c\":%d,\"tmp\":%d,\"v\":%d,\"s\":\"%s\"}\n",
            (long)now, bat_capacity, bat_temp, bat_volt, bat_status);
    fclose(f);
    last_save = now;
}

/* 返回历史 JSON 数组（只读尾 HIST_MAX 行）*/
static char *history_json(void) {
    FILE *f = fopen(HISTORY, "r");
    if (!f) return strdup("[]");
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    if (sz <= 0 || sz > (1 << 20)) { fclose(f); return strdup("[]"); }
    char *buf = malloc(sz + 1);
    size_t rd = fread(buf, 1, sz, f);
    fclose(f);
    buf[rd] = '\0';

    /* 统计行数，跳过最早的超出部分 */
    int nl = 0;
    for (char *s = buf; *s; s++) if (*s == '\n') nl++;
    int keep = nl > HIST_MAX ? nl - HIST_MAX : 0;

    /* 定位第 keep 个换行之后 */
    char *start = buf;
    int seen = 0;
    for (char *s = buf; *s && seen < keep; s++) {
        if (*s == '\n') { start = s + 1; seen++; }
    }

    /* 行间分隔符 \n -> ,  （合法 JSON）*/
    for (char *p = start; *p; p++) {
        if (*p == '\n') *p = ',';
    }
    size_t blen = strlen(start);
    if (blen > 0 && start[blen - 1] == ',') start[blen - 1] = '\0', blen--;

    char *out = malloc(blen + 3);
    out[0] = '[';
    memcpy(out + 1, start, blen);
    out[blen + 1] = ']';
    out[blen + 2] = '\0';
    free(buf);
    return out;
}

/* ---------- http ---------- */
static void send_resp(int fd, int code, const char *ctype, const char *body) {
    char head[512];
    int n = snprintf(head, sizeof(head),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "Cache-Control: no-store\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "\r\n",
        code, code == 200 ? "OK" : "Not Found", ctype, body ? strlen(body) : 0);
    ssize_t _w = write(fd, head, n); (void)_w;
    if (body) { ssize_t _w2 = write(fd, body, strlen(body)); (void)_w2; }
}

static void handle_status(int fd) {
    char body[512];
    snprintf(body, sizeof(body),
        "{\"capacity\":%d,\"temp\":%d,\"voltage\":%d,\"current\":%d,"
        "\"status\":\"%s\",\"limit_enabled\":%d,\"charge_limit\":%d,"
        "\"temp_limit\":%d,\"resume_delta\":%d}",
        bat_capacity, bat_temp, bat_volt, bat_curr, bat_status,
        limit_enabled, charge_limit, temp_limit, resume_delta);
    send_resp(fd, 200, "application/json", body);
}

static void handle_limit(int fd, const char *body) {
    if (body) {
        /* 解析 form: charge_limit=80&temp_limit=45&enabled=1 */
        char *copy = strdup(body);
        char *save = NULL;
        for (char *tok = strtok_r(copy, "&", &save); tok; tok = strtok_r(NULL, "&", &save)) {
            char *eq = strchr(tok, '=');
            if (!eq) continue;
            *eq = '\0';
            char key[64], val[64];
            strncpy(key, tok, sizeof(key) - 1); key[sizeof(key)-1] = '\0';
            strncpy(val, eq + 1, sizeof(val) - 1); val[sizeof(val)-1] = '\0';
            /* 直接使用 val，纯数字表单无需 URL 解码 */
            const char *dec = val;
            if      (!strcmp(key, "charge_limit")) charge_limit = atoi(dec);
            else if (!strcmp(key, "temp_limit"))   temp_limit = atoi(dec);
            else if (!strcmp(key, "resume_delta")) resume_delta = atoi(dec);
            else if (!strcmp(key, "enabled"))      limit_enabled = atoi(dec);
        }
        free(copy);
        save_config();
        apply_control();
    }
    send_resp(fd, 200, "application/json", "{\"ok\":true}");
}

static int serve_file(int fd, const char *uri) {
    char path[512];
    if (!strcmp(uri, "/") || !strcmp(uri, "/index.html"))
        snprintf(path, sizeof(path), "%s/index.html", WEBROOT);
    else
        snprintf(path, sizeof(path), "%s%s", WEBROOT, uri);
    /* 防目录穿越 */
    if (strstr(path, "..")) { send_resp(fd, 404, "text/plain", "Not Found"); return -1; }

    FILE *f = fopen(path, "rb");
    if (!f) { send_resp(fd, 404, "text/plain", "Not Found"); return -1; }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *body = malloc(len + 1);
    size_t rd = fread(body, 1, len, f);
    fclose(f);
    body[rd] = '\0';

    const char *ct = "text/plain";
    if      (strstr(uri, ".html")) ct = "text/html";
    else if (strstr(uri, ".css"))  ct = "text/css";
    else if (strstr(uri, ".js"))   ct = "application/javascript";
    else if (strstr(uri, ".svg"))  ct = "image/svg+xml";
    else if (strstr(uri, ".png"))  ct = "image/png";
    else if (strstr(uri, ".ico"))  ct = "image/x-icon";

    char head[512];
    int n = snprintf(head, sizeof(head),
        "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\nAccess-Control-Allow-Origin: *\r\n\r\n",
        ct, rd);
    ssize_t _w = write(fd, head, n); (void)_w;
    ssize_t _w3 = write(fd, body, rd); (void)_w3;
    free(body);
    return 0;
}

static void handle_client(int fd) {
    char buf[MAX_LINE];
    int n = read(fd, buf, sizeof(buf) - 1);
    if (n <= 0) return;
    buf[n] = '\0';

    char method[16], uri[256];
    if (sscanf(buf, "%15s %255s", method, uri) != 2) return;

    /* 读取 body（POST）*/
    char *body = NULL;
    if (!strcmp(method, "POST")) {
        char *hp = strstr(buf, "\r\n\r\n");
        if (hp) body = hp + 4;
        /* body 从 \r\n\r\n 后取，fetch 一次到齐，不处理 Content-Length */
    }

    if (!strcmp(uri, "/api/status")) { handle_status(fd); return; }
    if (!strcmp(uri, "/api/limit") && !strcmp(method, "POST")) { handle_limit(fd, body); return; }
    if (!strcmp(uri, "/api/history")) {
        char *j = history_json();
        send_resp(fd, 200, "application/json", j);
        free(j);
        return;
    }
    serve_file(fd, uri);
}

static void on_sig(int sig) { (void)sig; running = 0; }

int main(void) {
    /* 读配置 */
    load_config();
    last_save = time(NULL);
    /* 立即刷一次电池数据 */
    update_battery();

    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) return 1;
    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(PORT);
    if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) < 0) return 1;
    if (listen(srv, 8) < 0) return 1;

    signal(SIGINT, on_sig);
    signal(SIGTERM, on_sig);

    time_t last_poll = 0;
    while (running) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(srv, &rfds);
        struct timeval tv = { 1, 0 };
        int s = select(srv + 1, &rfds, NULL, NULL, &tv);
        if (s < 0) { sleep(1); continue; }
        if (s > 0 && FD_ISSET(srv, &rfds)) {
            struct sockaddr_in cli;
            socklen_t cli_len = sizeof(cli);
            int cfd = accept(srv, (struct sockaddr *)&cli, &cli_len);
            if (cfd >= 0) {
                handle_client(cfd);
                close(cfd);
            }
        }
        /* 周期轮询 */
        time_t now = time(NULL);
        if (now - last_poll >= (time_t)interval) {
            last_poll = now;
            update_battery();
            apply_control();
            push_history();
        }
    }
    close(srv);
    return 0;
}
