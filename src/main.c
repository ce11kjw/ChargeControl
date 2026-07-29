/* ============================================
   ChargeControl - HTTP服务器 + SSE
   ============================================ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <fcntl.h>

#include "battery_discovery.h"
#include "cJSON.h"

// 端口
#define PORT 8080
#define MAX_CLIENTS 100
#define BUFFER_SIZE 8192

// 全局标志
volatile int g_running = 1;
volatile int g_temp_stopped_charging = 0;

// SSE客户端
typedef struct {
    int fd;
    int active;
    time_t connect_time;
} SSEClient;

SSEClient g_sse_clients[MAX_CLIENTS];
int g_sse_client_count = 0;
pthread_mutex_t g_sse_mutex = PTHREAD_MUTEX_INITIALIZER;

// ============================================
// 日志函数
// ============================================
void log_info(const char *fmt, ...) {
    time_t now = time(NULL);
    struct tm *lt = localtime(&now);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", lt);
    
    printf("[%s] ", ts);
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    fflush(stdout);
}

// ============================================
// HTTP 响应
// ============================================
void send_response(int fd, int status, const char *content_type, const char *body, size_t len) {
    char header[1024];
    snprintf(header, sizeof(header),
        "HTTP/1.1 %d OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type\r\n"
        "Connection: close\r\n"
        "\r\n",
        status, content_type, len);
    
    send(fd, header, strlen(header), 0);
    if (body && len > 0) {
        send(fd, body, len, 0);
    }
}

void send_json(int fd, int status, const char *json) {
    send_response(fd, status, "application/json", json, strlen(json));
}

void send_json_free(int fd, int status, char *json) {
    if (!json) {
        send_json(fd, 500, "{\"error\":\"internal error\"}");
        return;
    }
    send_json(fd, status, json);
    free(json);
}

// ============================================
// 电池数据获取
// ============================================
char *get_battery_json(int battery_index) {
    if (battery_index < 0 || battery_index >= g_battery_count) {
        return strdup("{\"error\":\"invalid battery index\"}");
    }
    
    BatteryPaths *paths = &g_battery_paths[battery_index];
    cJSON *root = cJSON_CreateObject();
    
    // 容量
    if (strlen(paths->capacity) > 0) {
        ReadResult r;
        safe_read_file(paths->capacity, &r);
        if (r.status == READ_OK && validate_capacity(r.value) == READ_OK) {
            cJSON_AddNumberToObject(root, "capacity", atoi(r.value));
        }
    }
    
    // 状态
    if (strlen(paths->status) > 0) {
        ReadResult r;
        safe_read_file(paths->status, &r);
        if (r.status == READ_OK) {
            cJSON_AddStringToObject(root, "status", r.value);
        }
    }
    
    // 温度
    if (strlen(paths->temp) > 0) {
        ReadResult r;
        safe_read_file(paths->temp, &r);
        if (r.status == READ_OK && validate_temperature(r.value) == READ_OK) {
            double temp = atof(r.value);
            // 转换为摄氏度
            if (temp > 100) temp = temp / 10.0;
            cJSON_AddNumberToObject(root, "temperature", temp);
        }
    }
    
    // 电压
    if (strlen(paths->voltage) > 0) {
        ReadResult r;
        safe_read_file(paths->voltage, &r);
        if (r.status == READ_OK && validate_voltage(r.value) == READ_OK) {
            long uv = strtol(r.value, NULL, 10);
            double mv = (labs(uv) > 100000) ? uv / 1000.0 : (double)uv;
            cJSON_AddNumberToObject(root, "voltage_mv", mv);
        }
    }
    
    // 电流
    if (strlen(paths->current) > 0) {
        ReadResult r;
        safe_read_file(paths->current, &r);
        if (r.status == READ_OK && validate_current(r.value) == READ_OK) {
            long ua = strtol(r.value, NULL, 10);
            double ma = (labs(ua) > 100000) ? ua / 1000.0 : (double)ua;
            cJSON_AddNumberToObject(root, "current_ma", ma);
        }
    }
    
    // 健康
    if (strlen(paths->health) > 0) {
        ReadResult r;
        safe_read_file(paths->health, &r);
        if (r.status == READ_OK) {
            cJSON_AddStringToObject(root, "health", r.value);
        }
    }
    
    // 充电开关
    if (strlen(paths->charging_enabled) > 0) {
        ReadResult r;
        safe_read_file(paths->charging_enabled, &r);
        if (r.status == READ_OK) {
            int enabled = (strcmp(r.value, "1") == 0 || 
                          strcasecmp(r.value, "enabled") == 0 ||
                          strcasecmp(r.value, "true") == 0);
            cJSON_AddBoolToObject(root, "charging_enabled", enabled);
        }
    }
    
    // 设计容量
    if (strlen(paths->charge_full_design) > 0) {
        ReadResult r;
        safe_read_file(paths->charge_full_design, &r);
        if (r.status == READ_OK && validate_integer(r.value, 0, 100000) == READ_OK) {
            cJSON_AddNumberToObject(root, "charge_full_design", atoi(r.value));
        }
    }
    
    // 当前容量
    if (strlen(paths->charge_full) > 0) {
        ReadResult r;
        safe_read_file(paths->charge_full, &r);
        if (r.status == READ_OK && validate_integer(r.value, 0, 100000) == READ_OK) {
            cJSON_AddNumberToObject(root, "charge_full", atoi(r.value));
        }
    }
    
    // 循环次数
    if (strlen(paths->cycle_count) > 0) {
        ReadResult r;
        safe_read_file(paths->cycle_count, &r);
        if (r.status == READ_OK && validate_integer(r.value, 0, 100000) == READ_OK) {
            cJSON_AddNumberToObject(root, "cycle_count", atoi(r.value));
        }
    }
    
    // 制造商
    if (strlen(paths->manufacturer) > 0) {
        ReadResult r;
        safe_read_file(paths->manufacturer, &r);
        if (r.status == READ_OK) {
            cJSON_AddStringToObject(root, "manufacturer", r.value);
        }
    }
    
    // 型号
    if (strlen(paths->model_name) > 0) {
        ReadResult r;
        safe_read_file(paths->model_name, &r);
        if (r.status == READ_OK) {
            cJSON_AddStringToObject(root, "model_name", r.value);
        }
    }
    
    // 电池技术
    if (strlen(paths->technology) > 0) {
        ReadResult r;
        safe_read_file(paths->technology, &r);
        if (r.status == READ_OK) {
            cJSON_AddStringToObject(root, "technology", r.value);
        }
    }
    
    // 芯片温度
    ReadResult chip_temp;
    read_chip_temperature(0, &chip_temp);
    if (chip_temp.status == READ_OK) {
        double temp = atof(chip_temp.value) / 1000.0;
        cJSON_AddNumberToObject(root, "chip_temp", temp);
    }
    
    // 时间戳
    cJSON_AddNumberToObject(root, "timestamp", time(NULL));
    
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json;
}

// ============================================
// SSE 相关
// ============================================
void add_sse_client(int fd) {
    pthread_mutex_lock(&g_sse_mutex);
    
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!g_sse_clients[i].active) {
            g_sse_clients[i].fd = fd;
            g_sse_clients[i].active = 1;
            g_sse_clients[i].connect_time = time(NULL);
            g_sse_client_count++;
            break;
        }
    }
    
    pthread_mutex_unlock(&g_sse_mutex);
}

void remove_sse_client(int fd) {
    pthread_mutex_lock(&g_sse_mutex);
    
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_sse_clients[i].active && g_sse_clients[i].fd == fd) {
            g_sse_clients[i].active = 0;
            g_sse_client_count--;
            break;
        }
    }
    
    pthread_mutex_unlock(&g_sse_mutex);
}

void broadcast_sse_event(const char *event, const char *data) {
    pthread_mutex_lock(&g_sse_mutex);
    
    char message[4096];
    snprintf(message, sizeof(message), "event: %s\ndata: %s\n\n", event, data);
    
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_sse_clients[i].active) {
            if (send(g_sse_clients[i].fd, message, strlen(message), MSG_NOSIGNAL) < 0) {
                // 发送失败，移除客户端
                close(g_sse_clients[i].fd);
                g_sse_clients[i].active = 0;
                g_sse_client_count--;
            }
        }
    }
    
    pthread_mutex_unlock(&g_sse_mutex);
}

// SSE 线程
void *sse_thread(void *arg) {
    log_info("SSE线程启动");
    
    while (g_running) {
        // 广播电池数据
        if (g_battery_count > 0 && g_sse_client_count > 0) {
            char *json = get_battery_json(0);
            broadcast_sse_event("battery_update", json);
            free(json);
        }
        
        sleep(2); // 每2秒推送一次
    }
    
    return NULL;
}

// ============================================
// HTTP 处理
// ============================================
void handle_sse(int fd) {
    // 发送 SSE 头
    const char *headers = 
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/event-stream\r\n"
        "Cache-Control: no-cache\r\n"
        "Connection: keep-alive\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "\r\n";
    
    send(fd, headers, strlen(headers), 0);
    
    // 添加到客户端列表
    add_sse_client(fd);
    
    // 保持连接
    while (g_running) {
        sleep(1);
    }
    
    remove_sse_client(fd);
}

void handle_get_battery(int fd) {
    char *json = get_battery_json(0);
    send_json_free(fd, 200, json);
}

void handle_get_settings(int fd) {
    // 获取所有设置
    cJSON *root = cJSON_CreateObject();
    cJSON *battery = cJSON_Parse(get_battery_json(0));
    cJSON_AddItemToObject(root, "battery", battery);
    
    // 配置信息
    cJSON *config = cJSON_CreateObject();
    cJSON *charging = cJSON_CreateObject();
    cJSON_AddNumberToObject(charging, "max_limit", 80);
    cJSON_AddStringToObject(charging, "mode", "standard");
    cJSON_AddItemToObject(config, "charging", charging);
    cJSON_AddItemToObject(root, "config", config);
    
    // 历史数据（简化）
    cJSON *history = cJSON_CreateObject();
    cJSON_AddNumberToObject(history, "today_charges", 3);
    cJSON_AddStringToObject(history, "today_duration", "4h32m");
    cJSON_AddNumberToObject(history, "today_usage", 45);
    cJSON_AddNumberToObject(history, "avg_power", 12.3);
    cJSON_AddNumberToObject(history, "battery_wear", 5);
    cJSON_AddItemToObject(root, "history", history);
    
    // 电池数量
    cJSON_AddNumberToObject(root, "battery_count", g_battery_count);
    
    // 扩展信息
    cJSON *extended = cJSON_CreateObject();
    
    // 温度信息
    cJSON *temps = cJSON_CreateObject();
    ReadResult r;
    
    read_chip_temperature(0, &r);
    if (r.status == READ_OK) {
        cJSON_AddNumberToObject(temps, "cpu", atof(r.value) / 1000.0);
    }
    
    read_chip_temperature(3, &r);
    if (r.status == READ_OK) {
        cJSON_AddNumberToObject(temps, "gpu", atof(r.value) / 1000.0);
    }
    
    read_chip_temperature(5, &r);
    if (r.status == READ_OK) {
        cJSON_AddNumberToObject(temps, "board", atof(r.value) / 1000.0);
    }
    
    cJSON_AddItemToObject(extended, "temperatures", temps);
    
    // CPU信息
    cJSON *cpu = cJSON_CreateObject();
    read_cpu_cores(&r);
    if (r.status == READ_OK) {
        cJSON_AddNumberToObject(cpu, "cores", atoi(r.value));
    }
    read_cpu_freq(&r);
    if (r.status == READ_OK) {
        cJSON_AddNumberToObject(cpu, "freq_mhz", atoi(r.value));
    }
    cJSON_AddItemToObject(extended, "cpu", cpu);
    
    cJSON_AddItemToObject(root, "extended", extended);
    
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    send_json_free(fd, 200, json);
}

void handle_charging_mode(int fd, const char *body) {
    cJSON *root = cJSON_Parse(body);
    if (!root) {
        send_json(fd, 400, "{\"error\":\"invalid JSON\"}");
        return;
    }
    
    cJSON *mode = cJSON_GetObjectItem(root, "mode");
    if (!mode || !cJSON_IsString(mode)) {
        cJSON_Delete(root);
        send_json(fd, 400, "{\"error\":\"missing mode\"}");
        return;
    }
    
    log_info("切换充电模式: %s", mode->valuestring);
    
    cJSON_Delete(root);
    send_json(fd, 200, "{\"status\":\"ok\"}");
}

// ============================================
// 路由处理
// ============================================
void handle_request(int fd, const char *method, const char *path, const char *body) {
    // OPTIONS 预检请求
    if (strcmp(method, "OPTIONS") == 0) {
        send_response(fd, 200, "text/plain", "", 0);
        return;
    }
    
    // SSE 端点
    if (strcmp(path, "/api/events") == 0 && strcmp(method, "GET") == 0) {
        handle_sse(fd);
        return;
    }
    
    // 电池数据
    if (strcmp(path, "/api/battery") == 0 && strcmp(method, "GET") == 0) {
        handle_get_battery(fd);
        return;
    }
    
    // 设置数据
    if (strcmp(path, "/api/settings") == 0 && strcmp(method, "GET") == 0) {
        handle_get_settings(fd);
        return;
    }
    
    // 充电模式
    if (strcmp(path, "/api/charging/mode") == 0 && strcmp(method, "POST") == 0) {
        handle_charging_mode(fd, body);
        return;
    }
    
    // 默认：返回404
    send_json(fd, 404, "{\"error\":\"not found\"}");
}

// ============================================
// 客户端处理
// ============================================
void *client_thread(void *arg) {
    int fd = *(int *)arg;
    free(arg);
    
    char buffer[BUFFER_SIZE];
    int n = recv(fd, buffer, sizeof(buffer) - 1, 0);
    
    if (n <= 0) {
        close(fd);
        return NULL;
    }
    
    buffer[n] = '\0';
    
    // 解析请求
    char method[16] = {0};
    char path[256] = {0};
    char *body = NULL;
    
    sscanf(buffer, "%15s %255s", method, path);
    
    // 查找请求体
    char *body_start = strstr(buffer, "\r\n\r\n");
    if (body_start) {
        body_start += 4;
        body = body_start;
    }
    
    // 去除查询字符串
    char *query = strchr(path, '?');
    if (query) *query = '\0';
    
    // 处理请求
    handle_request(fd, method, path, body);
    
    close(fd);
    return NULL;
}

// ============================================
// 信号处理
// ============================================
void signal_handler(int sig) {
    log_info("收到信号 %d，正在退出...", sig);
    g_running = 0;
}

// ============================================
// 主函数
// ============================================
int main(int argc, char *argv[]) {
    // 注册信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    log_info("ChargeControl 启动中...");
    
    // 发现电池设备
    g_battery_count = discover_all_batteries();
    print_discovery_report();
    
    // 创建服务器套接字
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        log_info("创建套接字失败: %s", strerror(errno));
        return 1;
    }
    
    // 设置套接字选项
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    // 绑定地址
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);
    
    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        log_info("绑定端口失败: %s", strerror(errno));
        close(server_fd);
        return 1;
    }
    
    // 监听
    if (listen(server_fd, 10) < 0) {
        log_info("监听失败: %s", strerror(errno));
        close(server_fd);
        return 1;
    }
    
    log_info("HTTP服务器启动，端口: %d", PORT);
    log_info("访问: http://127.0.0.1:%d", PORT);
    
    // 启动 SSE 线程
    pthread_t sse_tid;
    pthread_create(&sse_tid, NULL, sse_thread, NULL);
    
    // 主循环
    while (g_running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            if (g_running) {
                log_info("接受连接失败: %s", strerror(errno));
            }
            continue;
        }
        
        // 创建客户端线程
        int *fd_ptr = malloc(sizeof(int));
        *fd_ptr = client_fd;
        
        pthread_t tid;
        pthread_create(&tid, NULL, client_thread, fd_ptr);
        pthread_detach(tid);
    }
    
    // 清理
    log_info("服务器关闭");
    close(server_fd);
    
    return 0;
}
