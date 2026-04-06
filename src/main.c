#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>

#include "config.h"
#include "charge_control.h"
#include "stats.h"
#include "snapshot_daemon.h"
#include "http_server.h"

/* ------------------------------------------------------------------ */
/* Defaults                                                            */
/* ------------------------------------------------------------------ */
#define DEFAULT_CONFIG_PATH "./config.json"
#define DEFAULT_DB_PATH     "./chargecontrol.db"
#define DEFAULT_PORT        8080
#define DEFAULT_WEBROOT     "./webroot"

/* ------------------------------------------------------------------ */
/* Signal handling                                                     */
/* ------------------------------------------------------------------ */

static volatile int g_shutdown = 0;

static void handle_signal(int sig)
{
    (void)sig;
    g_shutdown = 1;
    http_server_stop();
}

/* ------------------------------------------------------------------ */
/* Daemonise (--daemon flag)                                           */
/* ------------------------------------------------------------------ */

static void daemonize(void)
{
    pid_t pid;

    pid = fork();
    if (pid < 0) { perror("fork"); exit(1); }
    if (pid > 0) exit(0); /* parent exits */

    if (setsid() < 0) { perror("setsid"); exit(1); }

    /* Redirect stdio to /dev/null */
    {
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            if (devnull > 2) close(devnull);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Usage                                                               */
/* ------------------------------------------------------------------ */

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s [options]\n"
        "  --config <path>   Config file (default: %s)\n"
        "  --db <path>       SQLite database (default: %s)\n"
        "  --port <port>     HTTP port (default: %d)\n"
        "  --webroot <path>  Webroot directory (default: %s)\n"
        "  --daemon          Daemonise (background)\n"
        "  --help            Show this help\n",
        prog, DEFAULT_CONFIG_PATH, DEFAULT_DB_PATH, DEFAULT_PORT, DEFAULT_WEBROOT);
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
    const char *config_path = DEFAULT_CONFIG_PATH;
    const char *db_path     = DEFAULT_DB_PATH;
    const char *webroot     = DEFAULT_WEBROOT;
    int         port        = DEFAULT_PORT;
    int         do_daemon   = 0;
    int         i;
    cJSON      *cfg         = NULL;
    http_server_config_t srv_cfg;

    /* Parse arguments */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            config_path = argv[++i];
        } else if (strcmp(argv[i], "--db") == 0 && i + 1 < argc) {
            db_path = argv[++i];
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--webroot") == 0 && i + 1 < argc) {
            webroot = argv[++i];
        } else if (strcmp(argv[i], "--daemon") == 0) {
            do_daemon = 1;
        } else if (strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    /* Load config to check for overrides */
    cfg = config_load(config_path);
    if (cfg) {
        int cfg_port = config_get_int(cfg, "server", "port", 0);
        if (cfg_port > 0 && port == DEFAULT_PORT) port = cfg_port;
        cJSON_Delete(cfg);
    }

    /* Daemonise if requested */
    if (do_daemon) daemonize();

    /* Signal handling */
    signal(SIGTERM, handle_signal);
    signal(SIGINT,  handle_signal);
    signal(SIGHUP,  SIG_IGN);
    signal(SIGPIPE, SIG_IGN);

    /* Initialise database */
    if (stats_init_db(db_path) != 0) {
        fprintf(stderr, "[chargecontrol] Failed to init database: %s\n", db_path);
        /* Non-fatal: stats will be unavailable but server can still run */
    }

    /* Start snapshot daemon thread */
    snapshot_daemon_start(db_path, config_path, 30);

    /* Start HTTP server (blocks until stopped) */
    memset(&srv_cfg, 0, sizeof(srv_cfg));
    srv_cfg.port = port;
    strncpy(srv_cfg.webroot,     webroot,     sizeof(srv_cfg.webroot)     - 1);
    strncpy(srv_cfg.config_path, config_path, sizeof(srv_cfg.config_path) - 1);
    strncpy(srv_cfg.db_path,     db_path,     sizeof(srv_cfg.db_path)     - 1);

    fprintf(stdout, "[chargecontrol] Starting HTTP server on port %d\n", port);
    fflush(stdout);

    http_server_start(&srv_cfg);

    /* Graceful shutdown */
    fprintf(stdout, "[chargecontrol] Shutting down...\n");
    snapshot_daemon_stop();
    stats_close_db();

    return 0;
}
