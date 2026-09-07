/*
 * mempressure - a Linux answer to Android's onTrimMemory
 *
 * Subscribe your application to memory-pressure events derived from the
 * kernel's Pressure Stall Information (PSI). Instead of being killed by the
 * OOM killer (or userspace oomd), applications learn that memory is getting
 * tight and shed caches gracefully.
 */
#ifndef MEMPRESSURE_H
#define MEMPRESSURE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MP_VERSION "0.1.0"
#define MP_PSI_PATH_DEFAULT "/proc/pressure/memory"

typedef enum {
    MP_LEVEL_NONE = 0,     /* no meaningful stall time */
    MP_LEVEL_LOW = 1,      /* some reclaim happening; trim opportunistically */
    MP_LEVEL_MODERATE = 2, /* tasks visibly stalled; drop caches */
    MP_LEVEL_CRITICAL = 3  /* heavy stalls; shed everything, survive */
} mp_level_t;

typedef struct {
    double some_avg10;
    double some_avg60;
    double some_avg300;
    double full_avg10;
    double full_avg60;
    double full_avg300;
} mp_psi_t;

typedef void (*mp_callback_t)(mp_level_t level, void *userdata);

typedef struct {
    double low_threshold;      /* "some" avg10 in percent; default 5.0 */
    double moderate_threshold; /* default 15.0 */
    double critical_threshold; /* default 40.0 */
    double poll_interval_sec;  /* monitor tick; default 0.5 */
    int hysteresis;            /* consecutive readings before a level change
                                  fires; default 2 */
    const char *psi_path;      /* default /proc/pressure/memory; override for
                                  tests or containers */
} mp_config_t;

/* Start the monitor thread. cfg may be NULL for all defaults; zero fields in
 * cfg also fall back to their defaults (so partial overrides just work).
 * Returns 0, or -EALREADY if already running, -EINVAL for a bad config. */
int mp_init(const mp_config_t *cfg);

/* Stop the monitor thread and release everything. Idempotent. */
int mp_shutdown(void);

/* Current (post-hysteresis) pressure level. Valid after mp_init. */
mp_level_t mp_current_level(void);

/* Last observed PSI values; returns 0, or -EPERM if mp_init was not called. */
int mp_psi(mp_psi_t *out);

/* Register a callback fired from the monitor thread whenever the level
 * changes. Returns a handle (> 0), or -EINVAL (NULL cb), -EPERM (not
 * initialized). Callbacks must not block; do not call mp_shutdown from a
 * callback. */
int mp_subscribe(mp_callback_t cb, void *userdata);

/* Remove a subscription. Returns 0, or -ENOENT. Safe to call from within a
 * callback for any handle except the one currently being dispatched. */
int mp_unsubscribe(int handle);

const char *mp_level_name(mp_level_t level);
const char *mp_version(void);

#ifdef __cplusplus
}
#endif

#endif /* MEMPRESSURE_H */
