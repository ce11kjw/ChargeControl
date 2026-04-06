#include "snapshot_daemon.h"
#include "charge_control.h"
#include "stats.h"
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <time.h>

#define DEFAULT_INTERVAL_S 30

typedef struct {
    char    db_path[4096];
    char    config_path[4096];
    int     interval_s;
    volatile int running;
} daemon_ctx_t;

static daemon_ctx_t g_ctx;
static pthread_t    g_thread;
static int          g_started = 0;

/* ------------------------------------------------------------------ */
/* Daemon thread function                                              */
/* ------------------------------------------------------------------ */

static void *daemon_thread(void *arg)
{
    daemon_ctx_t *ctx = (daemon_ctx_t *)arg;

    /* Bug fix: ignore SIGHUP so the daemon survives terminal disconnects
     * (KernelSU environment) */
    signal(SIGHUP, SIG_IGN);

    while (ctx->running) {
        cJSON *cfg = NULL;
        battery_status_t bat;
        const char *mode = "normal";
        int i;

        if (cc_get_battery_status(&bat) == 0) {
            cfg = config_load(ctx->config_path);
            if (cfg) {
                const char *m = config_get_str(cfg, "charging", "mode", "normal");
                if (m) mode = m;
            }

            stats_record_snapshot(
                bat.capacity,
                bat.temperature,
                bat.voltage_mv,
                bat.current_ma,
                bat.status,
                mode
            );

            if (cfg) cJSON_Delete(cfg);
        }

        /* Sleep in 1-second increments so we can react to stop signals */
        for (i = 0; i < ctx->interval_s && ctx->running; i++) {
            sleep(1);
        }
    }

    return NULL;
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

void snapshot_daemon_start(const char *db_path, const char *config_path, int interval_s)
{
    if (g_started) return;

    memset(&g_ctx, 0, sizeof(g_ctx));

    if (db_path)     strncpy(g_ctx.db_path,     db_path,     sizeof(g_ctx.db_path)     - 1);
    if (config_path) strncpy(g_ctx.config_path, config_path, sizeof(g_ctx.config_path) - 1);
    g_ctx.interval_s = (interval_s > 0) ? interval_s : DEFAULT_INTERVAL_S;
    g_ctx.running    = 1;

    if (pthread_create(&g_thread, NULL, daemon_thread, &g_ctx) != 0) {
        g_ctx.running = 0;
        return;
    }

    g_started = 1;
}

void snapshot_daemon_stop(void)
{
    if (!g_started) return;
    g_ctx.running = 0;
    pthread_join(g_thread, NULL);
    g_started = 0;
}
