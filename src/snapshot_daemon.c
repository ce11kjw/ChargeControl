#include "snapshot_daemon.h"
#include "charge_control.h"
#include "stats.h"

#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

#define SNAPSHOT_INTERVAL_S 30

volatile sig_atomic_t daemon_running = 1;

static pthread_t s_thread;

static void *daemon_thread(void *arg)
{
    (void)arg;

    while (daemon_running) {
        BatteryStatus bs = cc_get_battery_status();

        ChargeConfig cfg;
        cc_load_config(&cfg);

        stats_record_snapshot(
            bs.capacity,
            bs.temperature,
            bs.voltage_mv >= 0 ? bs.voltage_mv : 0.0,
            bs.current_ma,
            bs.status,
            cfg.mode
        );

        /* Sleep in 1-second increments so we can react to daemon_running=0
           without waiting the full 30 seconds. */
        for (int i = 0; i < SNAPSHOT_INTERVAL_S && daemon_running; i++)
            sleep(1);
    }
    return NULL;
}

int snapshot_daemon_start(void)
{
    daemon_running = 1;
    return pthread_create(&s_thread, NULL, daemon_thread, NULL);
}

void snapshot_daemon_stop(void)
{
    daemon_running = 0;
    pthread_join(s_thread, NULL);
}
