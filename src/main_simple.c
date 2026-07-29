/* ChargeControl - 简化版 main.c */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>

/* 外部函数声明 */
extern char* get_battery_json(void);

#define PORT 8958
#define BUFFER_SIZE 4096

volatile int g_running = 1;

void handle_signal(int sig) {
    (void)sig;
    g_running = 0;
}

void send_response(int fd, int status, const char *content_type, const char *body) {
    char header[512];
    snprintf(header, sizeof(header),
        "HTTP/1.1 %d OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Connection: close\r\n"
        "\r\n",
        status, content_type, strlen(body));
    send(fd, header, strlen(header), 0);
    send(fd, body, strlen(body), 0);
}

void handle_request(int fd, const char *path) {
    if (strcmp(path, "/api/battery") == 0) {
        char *json = get_battery_json();
        send_response(fd, 200, "application/json", json);
        free(json);
    } else if (strcmp(path, "/api/settings") == 0) {
        char *json = get_battery_json();
        char response[1024];
        snprintf(response, sizeof(response), "{\"battery\":%s,\"config\":{}}", json);
        send_response(fd, 200, "application/json", response);
        free(json);
    } else {
        send_response(fd, 404, "application/json", "{\"error\":\"not found\"}");
    }
}

int main(void) {
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    
    printf("ChargeControl 启动中...\n");
    
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
            handle_request(client_fd, path);
        }
        
        close(client_fd);
    }
    
    close(server_fd);
    printf("服务器关闭\n");
    return 0;
}
