#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <time.h>

int main() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port = htons(8080)
    };
    
    bind(server_fd, (struct sockaddr *)&addr, sizeof(addr));
    listen(server_fd, 10);
    
    printf("测试服务启动，端口: 8080\n");
    
    while(1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        
        char buffer[4096];
        int n = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        if (n > 0) {
            buffer[n] = '\0';
            
            if (strstr(buffer, "/api/battery")) {
                char json[256];
                int cap = 40 + (time(NULL) % 51);
                double temp = 25.0 + (time(NULL) % 16);
                sprintf(json, "{\"capacity\":%d,\"temperature\":%.1f,\"timestamp\":%ld}", cap, temp, time(NULL));
                
                char header[256];
                sprintf(header, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %zu\r\n\r\n", strlen(json));
                send(client_fd, header, strlen(header), 0);
                send(client_fd, json, strlen(json), 0);
            }
        }
        close(client_fd);
    }
    
    close(server_fd);
    return 0;
}
