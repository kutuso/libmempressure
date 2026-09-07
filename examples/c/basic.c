/* Minimal libmempressure usage: shed caches as pressure rises. */
#include <mempressure.h>

#include <stdio.h>
#include <unistd.h>

static void on_pressure(mp_level_t level, void *userdata) {
    const char *app = userdata;
    printf("[%s] pressure: %s -> ", app, mp_level_name(level));
    switch (level) {
    case MP_LEVEL_NONE:
        printf("grow caches back\n");
        break;
    case MP_LEVEL_LOW:
        printf("stop prefetching\n");
        break;
    case MP_LEVEL_MODERATE:
        printf("drop object caches\n");
        break;
    case MP_LEVEL_CRITICAL:
        printf("shed everything, now\n");
        break;
    }
}

int main(void) {
    mp_config_t cfg = {0};
    cfg.poll_interval_sec = 1.0;
    if (mp_init(&cfg) != 0) {
        fprintf(stderr, "mp_init failed\n");
        return 1;
    }
    mp_subscribe(on_pressure, (void *)"demo");
    printf("watching %s with libmempressure %s (ctrl-c to stop)\n", MP_PSI_PATH_DEFAULT,
           mp_version());
    while (1) {
        pause();
    }
}
