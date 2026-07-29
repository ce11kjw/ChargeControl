/* ChargeControl - 完整版 main.c (SSE + 新UI) */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <pthread.h>

/* 外部函数声明 */
extern char* get_battery_json(void);
extern char* get_settings_json(void);
extern int set_charging_mode(const char *mode);

#define PORT 8959
#define BUFFER_SIZE 8192
#define MAX_SSE_CLIENTS 10

volatile int g_running = 1;

/* SSE 客户端管理 */
typedef struct {
    int fd;
    int active;
} SSEClient;

SSEClient g_sse_clients[MAX_SSE_CLIENTS];
int g_sse_count = 0;
pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;

void handle_signal(int sig) {
    (void)sig;
    g_running = 0;
}

/* ── HTTP 响应 ────────────────────────────────────────── */
void send_response(int fd, int status, const char *ctype, const char *body) {
    char header[512];
    snprintf(header, sizeof(header),
        "HTTP/1.1 %d OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        "Connection: close\r\n"
        "\r\n",
        status, ctype, strlen(body));
    send(fd, header, strlen(header), 0);
    if (body) send(fd, body, strlen(body), 0);
}

/* ── SSE 处理 ────────────────────────────────────────── */
void add_sse_client(int fd) {
    pthread_mutex_lock(&g_mutex);
    for (int i = 0; i < MAX_SSE_CLIENTS; i++) {
        if (!g_sse_clients[i].active) {
            g_sse_clients[i].fd = fd;
            g_sse_clients[i].active = 1;
            g_sse_count++;
            break;
        }
    }
    pthread_mutex_unlock(&g_mutex);
}

void broadcast_sse(const char *data) {
    char msg[2048];
    snprintf(msg, sizeof(msg), "data: %s\n\n", data);
    
    pthread_mutex_lock(&g_mutex);
    for (int i = 0; i < MAX_SSE_CLIENTS; i++) {
        if (g_sse_clients[i].active) {
            if (send(g_sse_clients[i].fd, msg, strlen(msg), MSG_NOSIGNAL) < 0) {
                close(g_sse_clients[i].fd);
                g_sse_clients[i].active = 0;
                g_sse_count--;
            }
        }
    }
    pthread_mutex_unlock(&g_mutex);
}

/* ── 请求处理 ────────────────────────────────────────── */
void handle_request(int fd, const char *method, const char *path, const char *body) {
    /* OPTIONS 预检 */
    if (strcmp(method, "OPTIONS") == 0) {
        send_response(fd, 200, "text/plain", "");
        return;
    }
    
    /* SSE 端点 */
    if (strcmp(path, "/api/events") == 0) {
        const char *header = 
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/event-stream\r\n"
            "Cache-Control: no-cache\r\n"
            "Connection: keep-alive\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "\r\n";
        send(fd, header, strlen(header), 0);
        add_sse_client(fd);
        
        /* 保持连接 */
        while (g_running) sleep(1);
        return;
    }
    
    /* 电池数据 */
    if (strcmp(path, "/api/battery") == 0) {
        char *json = get_battery_json();
        send_response(fd, 200, "application/json", json);
        free(json);
        return;
    }
    
    /* 设置数据 */
    if (strcmp(path, "/api/settings") == 0) {
        char *json = get_settings_json();
        send_response(fd, 200, "application/json", json);
        free(json);
        return;
    }
    
    /* 充电模式 */
    if (strcmp(path, "/api/charging/mode") == 0 && strcmp(method, "POST") == 0) {
        /* 简单解析 */
        if (strstr(body, "turbo")) set_charging_mode("turbo");
        else if (strstr(body, "fast")) set_charging_mode("fast");
        else if (strstr(body, "standard")) set_charging_mode("standard");
        else if (strstr(body, "trickle")) set_charging_mode("trickle");
        else if (strstr(body, "protect")) set_charging_mode("protect");
        send_response(fd, 200, "application/json", "{\"status\":\"ok\"}");
        return;
    }
    
    /* 默认 */
    send_response(fd, 404, "application/json", "{\"error\":\"not found\"}");
}

/* ── SSE 广播线程 ────────────────────────────────────── */
void* sse_thread(void *arg) {
    (void)arg;
    while (g_running) {
        if (g_sse_count > 0) {
            char *json = get_battery_json();
            broadcast_sse(json);
            free(json);
        }
        sleep(2);
    }
    return NULL;
}

/* ── 主函数 ────────────────────────────────────────── */
int main(void) {
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    
    printf("ChargeControl 启动中...\n");
    
    /* 启动 SSE 线程 */
    pthread_t tid;
    pthread_create(&tid, NULL, sse_thread, NULL);
    
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }
    
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port = htons(PORT)
    };
    
    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(server_fd);
        return 1;
    }
    
    listen(server_fd, 10);
    printf("HTTP服务器启动，端口: %d\n", PORT);
    printf("访问: http://127.0.0.1:%d\n", PORT);
    
    while (g_running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        
        if (client_fd < 0) continue;
        
        char buffer[BUFFER_SIZE];
        int n = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        if (n > 0) {
            buffer[n] = '\0';
            char method[16], path[256];
            sscanf(buffer, "%15s %255s", method, path);
            
            char *body = strstr(buffer, "\r\n\r\n");
            if (body) body += 4;
            
            handle_request(client_fd, method, path, body);
        }
        
        close(client_fd);
    }
    
    close(server_fd);
    printf("服务器关闭\n");
    return 0;
}
