#ifndef SNAPSHOT_DAEMON_H
#define SNAPSHOT_DAEMON_H

#include <signal.h>

/* Set to 0 to request the daemon to stop. */
extern volatile sig_atomic_t daemon_running;

/* Start the snapshot daemon in a new pthread.
   Returns 0 on success, non-zero on error. */
int snapshot_daemon_start(void);

/* Signal the daemon to stop and wait for the thread to finish. */
void snapshot_daemon_stop(void);

#endif /* SNAPSHOT_DAEMON_H */
