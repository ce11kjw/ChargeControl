#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include "cjson/cJSON.h"

typedef struct {
    int   port;           /* listening port (default 8080) */
    char  webroot[4096];  /* path to webroot/ directory */
    char  config_path[4096];
    char  db_path[4096];
} http_server_config_t;

/* Start HTTP server (blocks until http_server_stop() is called from another thread). */
int http_server_start(const http_server_config_t *cfg);

/* Signal the server to stop accepting connections. */
void http_server_stop(void);

#endif /* HTTP_SERVER_H */
