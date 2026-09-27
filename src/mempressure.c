#define _GNU_SOURCE

#include "mempressure.h"

#include <errno.h>
#include <locale.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MP_DEFAULT_PSI_PATH MP_PSI_PATH_DEFAULT

typedef struct sub {
    int handle;
    int refs;
    int canceled;
    mp_callback_t cb;
    void *userdata;
    struct sub *next;
} sub_t;

typedef struct {
    sub_t *sub;
    mp_callback_t cb;
    void *userdata;
} sub_snapshot_t;

static struct {
    pthread_mutex_t lock;
    pthread_cond_t cv;
    pthread_t thread;
    int started;
    int running;
    int stopping;
    mp_config_t cfg;
    char *psi_path;
    mp_psi_t psi;
    mp_level_t level;
    mp_level_t pending;
    int pending_count;
    int next_handle;
    sub_t *subs;
} g = {
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .cv = PTHREAD_COND_INITIALIZER,
};

static void wait_interval(double seconds) {
    if (seconds > 86400.0) {
        seconds = 86400.0;
    }
    struct timespec until;
    clock_gettime(CLOCK_REALTIME, &until);
    until.tv_sec += (time_t)seconds;
    until.tv_nsec += (long)((seconds - (double)(time_t)seconds) * 1e9);
    while (until.tv_nsec >= 1000000000L) {
        until.tv_nsec -= 1000000000L;
        until.tv_sec++;
    }
    pthread_mutex_lock(&g.lock);
    while (g.running) {
        if (pthread_cond_timedwait(&g.cv, &g.lock, &until) == ETIMEDOUT) {
            break;
        }
    }
    pthread_mutex_unlock(&g.lock);
}

static int parse_line(const char *line, const char *kind, mp_psi_t *out) {
    size_t klen = strlen(kind);
    if (strncmp(line, kind, klen) != 0) {
        return 0;
    }
    const char *cursor = line + klen;
    if (*cursor != ' ' && *cursor != '\t') {
        return 0;
    }
    static locale_t c_loc;
    if (!c_loc) {
        c_loc = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0);
    }
    int have_avg10 = 0;
    int have_avg60 = 0;
    int have_avg300 = 0;
    while (*cursor == ' ' || *cursor == '\t') cursor++;
    char key[8];
    while (sscanf(cursor, "%7[a-z0-9_]=", key) == 1) {
        cursor += strlen(key) + 1;
        char *end = NULL;
        double value = c_loc ? strtod_l(cursor, &end, c_loc) : strtod(cursor, &end);
        if (end == cursor) {
            break;
        }
        if (isfinite(value)) {
            int is_some = strcmp(kind, "some") == 0;
            if (strcmp(key, "avg10") == 0 && !have_avg10) {
                if (is_some) out->some_avg10 = value; else out->full_avg10 = value;
                have_avg10 = 1;
            } else if (strcmp(key, "avg60") == 0 && !have_avg60) {
                if (is_some) out->some_avg60 = value; else out->full_avg60 = value;
                have_avg60 = 1;
            } else if (strcmp(key, "avg300") == 0 && !have_avg300) {
                if (is_some) out->some_avg300 = value; else out->full_avg300 = value;
                have_avg300 = 1;
            }
        }
        cursor = end;
        while (*cursor && *cursor != ' ' && *cursor != '\t') cursor++;
        while (*cursor == ' ' || *cursor == '\t') cursor++;
        if (!*cursor) break;
    }
    return have_avg10 && have_avg60 && have_avg300;
}

static int read_psi(const char *path, mp_psi_t *out) {
    FILE *fh = fopen(path, "r");
    if (!fh) {
        return -errno;
    }
    memset(out, 0, sizeof *out);
    char line[256];
    int have_some = 0;
    int have_full = 0;
    while (fgets(line, sizeof line, fh)) {
        if (parse_line(line, "some", out)) have_some = 1;
        if (parse_line(line, "full", out)) have_full = 1;
    }
    fclose(fh);
    return (have_some && have_full) ? 0 : -EIO;
}

static mp_level_t level_for(const mp_psi_t *psi, const mp_config_t *cfg) {
    double some = psi->some_avg10;
    if (some >= cfg->critical_threshold) return MP_LEVEL_CRITICAL;
    if (some >= cfg->moderate_threshold) return MP_LEVEL_MODERATE;
    if (some >= cfg->low_threshold) return MP_LEVEL_LOW;
    return MP_LEVEL_NONE;
}

static void *monitor_main(void *arg) {
    (void)arg;
    const mp_config_t cfg = g.cfg;
    const char *psi_path = g.psi_path;
    while (1) {
        pthread_mutex_lock(&g.lock);
        int running = g.running;
        pthread_mutex_unlock(&g.lock);
        if (!running) {
            break;
        }

        mp_psi_t psi;
        if (read_psi(psi_path, &psi) == 0) {
            mp_level_t reading = level_for(&psi, &cfg);

            pthread_mutex_lock(&g.lock);
            g.psi = psi;
            if (reading != g.pending) {
                g.pending = reading;
                g.pending_count = 1;
            } else if (g.pending_count < cfg.hysteresis) {
                g.pending_count++;
            }

            sub_snapshot_t *fire_subs = NULL;
            int fire_count = 0;
            mp_level_t fire_level = g.level;
            if (g.pending_count >= cfg.hysteresis && g.pending != g.level) {
                int n = 0;
                for (sub_t *s = g.subs; s; s = s->next) n++;
                if (n > 0) {
                    fire_subs = malloc((size_t)n * sizeof *fire_subs);
                    if (fire_subs) {
                        fire_count = n;
                        int i = 0;
                        for (sub_t *s = g.subs; s; s = s->next) {
                            s->refs++;
                            fire_subs[i].sub = s;
                            fire_subs[i].cb = s->cb;
                            fire_subs[i].userdata = s->userdata;
                            i++;
                        }
                    }
                }
                if (n == 0 || fire_subs) {
                    g.level = g.pending;
                    fire_level = g.level;
                }
            }
            pthread_mutex_unlock(&g.lock);

            for (int i = 0; i < fire_count; i++) {
                sub_t *s = fire_subs[i].sub;
                pthread_mutex_lock(&g.lock);
                int skip = s->canceled;
                pthread_mutex_unlock(&g.lock);
                if (!skip) {
                    fire_subs[i].cb(fire_level, fire_subs[i].userdata);
                }
                pthread_mutex_lock(&g.lock);
                s->refs--;
                int free_now = s->refs == 0 && s->canceled;
                pthread_cond_broadcast(&g.cv);
                pthread_mutex_unlock(&g.lock);
                if (free_now) {
                    free(s);
                }
            }
            free(fire_subs);
        }
        wait_interval(cfg.poll_interval_sec);
    }
    return NULL;
}

static int config_valid(const mp_config_t *cfg) {
    if (!isfinite(cfg->low_threshold) || !isfinite(cfg->moderate_threshold) ||
        !isfinite(cfg->critical_threshold) || !isfinite(cfg->poll_interval_sec)) {
        return 0;
    }
    if (cfg->low_threshold < 0 || cfg->moderate_threshold <= cfg->low_threshold ||
        cfg->critical_threshold <= cfg->moderate_threshold || cfg->critical_threshold > 100) {
        return 0;
    }
    if (cfg->poll_interval_sec <= 0 || cfg->poll_interval_sec > 86400.0) {
        return 0;
    }
    if (cfg->hysteresis < 1 || cfg->hysteresis > 1000) {
        return 0;
    }
    return 1;
}

static void config_fill_defaults(mp_config_t *cfg) {
    if (cfg->low_threshold == 0) {
        cfg->low_threshold = 5.0;
    }
    if (cfg->moderate_threshold == 0) {
        cfg->moderate_threshold = 15.0;
    }
    if (cfg->critical_threshold == 0) {
        cfg->critical_threshold = 40.0;
    }
    if (cfg->poll_interval_sec == 0) {
        cfg->poll_interval_sec = 0.5;
    }
    if (cfg->hysteresis == 0) {
        cfg->hysteresis = 2;
    }
}

int mp_init(const mp_config_t *cfg) {
    pthread_mutex_lock(&g.lock);
    if (g.started) {
        pthread_mutex_unlock(&g.lock);
        return -EALREADY;
    }
    if (g.stopping) {
        pthread_mutex_unlock(&g.lock);
        return -EAGAIN;
    }
    memset(&g.cfg, 0, sizeof g.cfg);
    if (cfg) {
        g.cfg = *cfg;
    }
    config_fill_defaults(&g.cfg);
    if (!config_valid(&g.cfg)) {
        pthread_mutex_unlock(&g.lock);
        return -EINVAL;
    }
    g.psi_path = strdup(cfg && cfg->psi_path ? cfg->psi_path : MP_DEFAULT_PSI_PATH);
    if (!g.psi_path) {
        pthread_mutex_unlock(&g.lock);
        return -ENOMEM;
    }
    mp_psi_t initial;
    int rc = read_psi(g.psi_path, &initial);
    if (rc != 0) {
        free(g.psi_path);
        g.psi_path = NULL;
        pthread_mutex_unlock(&g.lock);
        return rc;
    }
    g.psi = initial;
    g.level = MP_LEVEL_NONE;
    g.pending = MP_LEVEL_NONE;
    g.pending_count = 0;
    g.next_handle = 1;
    g.subs = NULL;
    g.running = 1;
    g.started = 1;
    int create_rc = pthread_create(&g.thread, NULL, monitor_main, NULL);
    if (create_rc != 0) {
        free(g.psi_path);
        g.psi_path = NULL;
        g.started = 0;
        g.running = 0;
        pthread_mutex_unlock(&g.lock);
        return -create_rc;
    }
    pthread_mutex_unlock(&g.lock);
    return 0;
}

int mp_shutdown(void) {
    pthread_mutex_lock(&g.lock);
    while (1) {
        if (!g.started && !g.stopping) {
            pthread_mutex_unlock(&g.lock);
            return 0;
        }
        if (g.started && pthread_equal(pthread_self(), g.thread)) {
            pthread_mutex_unlock(&g.lock);
            return -EDEADLK;
        }
        if (!g.stopping) {
            break;
        }
        pthread_cond_wait(&g.cv, &g.lock);
    }
    g.stopping = 1;
    g.running = 0;
    pthread_t thread = g.thread;
    sub_t *subs = g.subs;
    g.subs = NULL;
    char *psi_path = g.psi_path;
    g.psi_path = NULL;
    g.started = 0;
    pthread_cond_broadcast(&g.cv);
    pthread_mutex_unlock(&g.lock);

    int rc = pthread_join(thread, NULL);
    while (subs) {
        sub_t *next = subs->next;
        free(subs);
        subs = next;
    }
    free(psi_path);
    pthread_mutex_lock(&g.lock);
    g.level = MP_LEVEL_NONE;
    g.psi = (mp_psi_t){0};
    g.stopping = 0;
    pthread_cond_broadcast(&g.cv);
    pthread_mutex_unlock(&g.lock);
    return rc == 0 ? 0 : -rc;
}

mp_level_t mp_current_level(void) {
    pthread_mutex_lock(&g.lock);
    mp_level_t level = g.level;
    pthread_mutex_unlock(&g.lock);
    return level;
}

int mp_psi(mp_psi_t *out) {
    if (!out) {
        return -EINVAL;
    }
    pthread_mutex_lock(&g.lock);
    if (!g.started) {
        pthread_mutex_unlock(&g.lock);
        return -EPERM;
    }
    *out = g.psi;
    pthread_mutex_unlock(&g.lock);
    return 0;
}

int mp_subscribe(mp_callback_t cb, void *userdata) {
    if (!cb) {
        return -EINVAL;
    }
    pthread_mutex_lock(&g.lock);
    if (!g.started) {
        pthread_mutex_unlock(&g.lock);
        return -EPERM;
    }
    sub_t *sub = malloc(sizeof *sub);
    if (!sub) {
        pthread_mutex_unlock(&g.lock);
        return -ENOMEM;
    }
    if (g.next_handle < 0) {
        free(sub);
        pthread_mutex_unlock(&g.lock);
        return -ENOSPC;
    }
    sub->handle = g.next_handle++;
    sub->refs = 1;
    sub->canceled = 0;
    sub->cb = cb;
    sub->userdata = userdata;
    sub->next = g.subs;
    g.subs = sub;
    int handle = sub->handle;
    pthread_mutex_unlock(&g.lock);
    return handle;
}

int mp_unsubscribe(int handle) {
    pthread_mutex_lock(&g.lock);
    sub_t **cursor = &g.subs;
    while (*cursor && (*cursor)->handle != handle) {
        cursor = &(*cursor)->next;
    }
    if (!*cursor) {
        pthread_mutex_unlock(&g.lock);
        return -ENOENT;
    }
    sub_t *dead = *cursor;
    *cursor = dead->next;
    dead->next = NULL;
    dead->refs--;
    if (pthread_equal(pthread_self(), g.thread)) {
        dead->canceled = 1;
        int free_now = dead->refs == 0;
        pthread_mutex_unlock(&g.lock);
        if (free_now) {
            free(dead);
        }
        return 0;
    }
    while (dead->refs > 0) {
        pthread_cond_wait(&g.cv, &g.lock);
    }
    pthread_mutex_unlock(&g.lock);
    free(dead);
    return 0;
}

const char *mp_level_name(mp_level_t level) {
    switch (level) {
    case MP_LEVEL_NONE:
        return "none";
    case MP_LEVEL_LOW:
        return "low";
    case MP_LEVEL_MODERATE:
        return "moderate";
    case MP_LEVEL_CRITICAL:
        return "critical";
    default:
        return "unknown";
    }
}

const char *mp_version(void) {
    return MP_VERSION;
}
