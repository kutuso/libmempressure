/* Test runner for libmempressure core. Builds with the library and runs
 * under ctest; uses configurable psi_path fixtures, so no real pressure is
 * needed. Exit code 0 = all pass. */

#include "mempressure.h"

#include <errno.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures = 0;
static const char *current_test = "";

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL %s: %s (%s:%d)\n", current_test, #cond,      \
                    __FILE__, __LINE__);                                       \
            failures++;                                                        \
        }                                                                      \
    } while (0)

static char tmpdir[64];

static const char *fx(const char *name) {
    static char bufs[8][128];
    static int next;
    char *buf = bufs[next++ % 8];
    snprintf(buf, sizeof bufs[0], "%s/%s.psi", tmpdir, name);
    return buf;
}

static void cleanup_tmpdir(void) {
    DIR *dir = opendir(tmpdir);
    if (dir) {
        struct dirent *ent;
        while ((ent = readdir(dir))) {
            char path[sizeof tmpdir + 300];
            snprintf(path, sizeof path, "%s/%s", tmpdir, ent->d_name);
            unlink(path);
        }
        closedir(dir);
    }
    rmdir(tmpdir);
}

static void write_raw(const char *path, const char *text) {
    FILE *fh = fopen(path, "w");
    if (!fh) {
        perror(path);
        exit(2);
    }
    fputs(text, fh);
    fclose(fh);
}

static void write_psi(const char *path, double some10, double full10) {
    FILE *fh = fopen(path, "w");
    if (!fh) {
        perror(path);
        exit(2);
    }
    fprintf(fh,
            "some avg10=%.2f avg60=0.00 avg300=0.00 total=0\n"
            "full avg10=%.2f avg60=0.00 avg300=0.00 total=0\n",
            some10, full10);
    fclose(fh);
}

static mp_config_t test_config(const char *path) {
    mp_config_t cfg = {0};
    cfg.low_threshold = 5.0;
    cfg.moderate_threshold = 15.0;
    cfg.critical_threshold = 40.0;
    cfg.poll_interval_sec = 0.02;
    cfg.hysteresis = 1;
    cfg.psi_path = path;
    return cfg;
}

/* wait up to timeout_ms for cond; polls every 5ms */
#define WAIT_FOR(cond, timeout_ms)                                             \
    ({                                                                         \
        int _ok = 0;                                                           \
        for (int _i = 0; _i < (timeout_ms) / 5; _i++) {                        \
            if (cond) {                                                        \
                _ok = 1;                                                       \
                break;                                                         \
            }                                                                  \
            usleep(5000);                                                      \
        }                                                                      \
        _ok;                                                                   \
    })

static void test_parse(void) {
    current_test = "parse";
    const char *path = fx("parse");
    write_psi(path, 3.5, 1.25);
    mp_config_t defaults = {.psi_path = path};
    CHECK(mp_init(&defaults) == 0);
    mp_shutdown();

    mp_config_t cfg = test_config(path);
    CHECK(mp_init(&cfg) == 0);
    mp_psi_t psi = {0};
    CHECK(WAIT_FOR(mp_psi(&psi) == 0 && psi.some_avg10 == 3.5, 2000));
    CHECK(psi.some_avg60 == 0.0);
    CHECK(psi.full_avg10 == 1.25);
    CHECK(psi.full_avg300 == 0.0);
    mp_shutdown();
}

static void test_parse_robustness(void) {
    current_test = "parse-robustness";
    const char *path = fx("robust");
    mp_config_t cfg = test_config(path);
    mp_psi_t psi = {0};

    write_raw(path,
              "some a_very_long_future_key=1 avg10=2.50 avg60=3.00 avg300=4.00 total=9\r\n"
              "full avg10=0.50 avg60=0.00 avg300=0.00 total=0 extra=7\n");
    CHECK(mp_init(&cfg) == 0);
    CHECK(mp_psi(&psi) == 0);
    CHECK(psi.some_avg10 == 2.5 && psi.some_avg60 == 3.0 && psi.some_avg300 == 4.0);
    CHECK(psi.full_avg10 == 0.5);
    mp_shutdown();

    write_raw(path,
              "some avg10=-2.00 avg60=0.00 avg300=0.00 total=0\n"
              "full avg10=0.00 avg60=0.00 avg300=0.00 total=0\n");
    CHECK(mp_init(&cfg) == -EIO);

    write_raw(path,
              "some avg10=1e1 avg60=0.00 avg300=0.00 total=0\n"
              "full avg10=0.00 avg60=0.00 avg300=0.00 total=0\n");
    CHECK(mp_init(&cfg) == -EIO);

    write_raw(path,
              "some avg10=nan avg60=0.00 avg300=0.00 total=0\n"
              "full avg10=0.00 avg60=0.00 avg300=0.00 total=0\n");
    CHECK(mp_init(&cfg) == -EIO);

    write_raw(path,
              "some avg10=250.00 avg60=0.00 avg300=0.00 total=0\n"
              "full avg10=0.00 avg60=0.00 avg300=0.00 total=0\n");
    CHECK(mp_init(&cfg) == -EIO);

    write_raw(path, "some avg10=1.00 avg60=0.00 avg300=0.00 total=0\n");
    CHECK(mp_init(&cfg) == -EIO);

    write_raw(path, "");
    CHECK(mp_init(&cfg) == -EIO);

    write_raw(path,
              "somewhat avg10=1.00 avg60=0.00 avg300=0.00\n"
              "fullest avg10=1.00 avg60=0.00 avg300=0.00\n");
    CHECK(mp_init(&cfg) == -EIO);
    CHECK(mp_shutdown() == 0);
}

static void test_levels(void) {
    current_test = "levels";
    const char *path = fx("levels");
    write_psi(path, 0.0, 0.0);
    mp_config_t cfg = test_config(path);
    CHECK(mp_init(&cfg) == 0);
    CHECK(WAIT_FOR(mp_current_level() == MP_LEVEL_NONE, 2000));

    write_psi(path, 8.0, 0.0);
    CHECK(WAIT_FOR(mp_current_level() == MP_LEVEL_LOW, 2000));
    write_psi(path, 20.0, 0.0);
    CHECK(WAIT_FOR(mp_current_level() == MP_LEVEL_MODERATE, 2000));
    write_psi(path, 50.0, 0.0);
    CHECK(WAIT_FOR(mp_current_level() == MP_LEVEL_CRITICAL, 2000));
    CHECK(strcmp(mp_level_name(MP_LEVEL_CRITICAL), "critical") == 0);
    mp_shutdown();
}

static atomic_int callback_hits;
static atomic_int callback_level;

static void on_level(mp_level_t level, void *userdata) {
    (void)userdata;
    atomic_fetch_add(&callback_hits, 1);
    atomic_store(&callback_level, (int)level);
}

static void test_callback_fires_on_change(void) {
    current_test = "callback";
    const char *path = fx("callback");
    write_psi(path, 0.0, 0.0);
    mp_config_t cfg = test_config(path);
    CHECK(mp_init(&cfg) == 0);
    atomic_store(&callback_hits, 0);
    atomic_store(&callback_level, -1);
    int handle = mp_subscribe(on_level, NULL);
    CHECK(handle > 0);

    write_psi(path, 60.0, 0.0);
    CHECK(WAIT_FOR(atomic_load(&callback_level) == MP_LEVEL_CRITICAL, 2000));

    write_psi(path, 0.0, 0.0);
    CHECK(WAIT_FOR(atomic_load(&callback_level) == MP_LEVEL_NONE, 2000));
    CHECK(atomic_load(&callback_hits) >= 2);
    mp_shutdown();
    mp_unsubscribe(handle);
}

static int self_handle_value;

static void unsub_self(mp_level_t level, void *userdata) {
    (void)level;
    (void)userdata;
    mp_unsubscribe(self_handle_value);
}

static void test_unsubscribe_from_callback(void) {
    current_test = "unsubscribe-from-callback";
    const char *path = fx("unsub");
    write_psi(path, 0.0, 0.0);
    mp_config_t cfg = test_config(path);
    CHECK(mp_init(&cfg) == 0);

    self_handle_value = mp_subscribe(unsub_self, NULL);
    CHECK(self_handle_value > 0);

    write_psi(path, 60.0, 0.0);
    CHECK(WAIT_FOR(mp_current_level() == MP_LEVEL_CRITICAL, 2000));
    CHECK(mp_unsubscribe(self_handle_value) == -ENOENT);
    mp_shutdown();
}

static atomic_int shutdown_rc;

static void shutdown_cb(mp_level_t level, void *userdata) {
    (void)level;
    (void)userdata;
    atomic_store(&shutdown_rc, mp_shutdown());
}

static void test_shutdown_from_callback(void) {
    current_test = "shutdown-from-callback";
    const char *path = fx("shutcb");
    write_psi(path, 0.0, 0.0);
    mp_config_t cfg = test_config(path);
    CHECK(mp_init(&cfg) == 0);
    atomic_store(&shutdown_rc, 1);
    CHECK(mp_subscribe(shutdown_cb, NULL) > 0);
    write_psi(path, 60.0, 0.0);
    CHECK(WAIT_FOR(atomic_load(&shutdown_rc) != 1, 2000));
    CHECK(atomic_load(&shutdown_rc) == -EDEADLK);
    CHECK(mp_shutdown() == 0);
}

static atomic_int quiesce_hits;

static void quiesce_cb(mp_level_t level, void *userdata) {
    (void)level;
    (void)userdata;
    atomic_fetch_add(&quiesce_hits, 1);
}

static void test_unsubscribe_quiesces(void) {
    current_test = "unsubscribe-quiesces";
    const char *path = fx("quiesce");
    write_psi(path, 0.0, 0.0);
    mp_config_t cfg = test_config(path);
    CHECK(mp_init(&cfg) == 0);
    atomic_store(&quiesce_hits, 0);
    int handle = mp_subscribe(quiesce_cb, NULL);
    CHECK(handle > 0);

    write_psi(path, 60.0, 0.0);
    CHECK(WAIT_FOR(atomic_load(&quiesce_hits) >= 1, 2000));

    CHECK(mp_unsubscribe(handle) == 0);
    int after = atomic_load(&quiesce_hits);
    usleep(200000);
    CHECK(atomic_load(&quiesce_hits) == after);

    write_psi(path, 0.0, 0.0);
    usleep(100000);
    CHECK(atomic_load(&quiesce_hits) == after);
    mp_shutdown();
}

static void test_lifecycle(void) {
    current_test = "lifecycle";
    const char *path = fx("lifecycle");
    write_psi(path, 0.0, 0.0);
    mp_config_t cfg = test_config(path);

    mp_psi_t psi = {0};
    CHECK(mp_psi(&psi) == -EPERM);
    CHECK(mp_subscribe(on_level, NULL) == -EPERM);
    CHECK(mp_init(&(mp_config_t){.low_threshold = -1}) == -EINVAL);
    CHECK(mp_init(&(mp_config_t){.critical_threshold = 1.0}) == -EINVAL);
    CHECK(mp_init(&(mp_config_t){.poll_interval_sec = -1}) == -EINVAL);
    CHECK(mp_init(&(mp_config_t){.hysteresis = -3}) == -EINVAL);
    CHECK(mp_init(&(mp_config_t){.low_threshold = 7, .psi_path = path}) == 0);
    mp_psi_t partial = {0};
    CHECK(mp_psi(&partial) == 0 && partial.some_avg10 == 0.0);
    CHECK(mp_shutdown() == 0);
    cfg.psi_path = fx("missing");
    unlink(cfg.psi_path);
    CHECK(mp_init(&cfg) == -ENOENT);
    cfg.psi_path = path;

    CHECK(mp_init(&cfg) == 0);
    CHECK(mp_init(&cfg) == -EALREADY);
    CHECK(mp_psi(&psi) == 0);

    CHECK(mp_shutdown() == 0);
    CHECK(mp_shutdown() == 0);
    CHECK(mp_psi(&psi) == -EPERM);
    CHECK(mp_unsubscribe(12345) == -ENOENT);
    CHECK(strcmp(mp_version(), MP_VERSION) == 0);
}

int main(void) {
    snprintf(tmpdir, sizeof tmpdir, "/tmp/mp_test.XXXXXX");
    if (!mkdtemp(tmpdir)) {
        perror("mkdtemp");
        return 2;
    }
    atexit(cleanup_tmpdir);
    test_parse();
    test_parse_robustness();
    test_levels();
    test_callback_fires_on_change();
    test_unsubscribe_from_callback();
    test_unsubscribe_quiesces();
    test_shutdown_from_callback();
    test_lifecycle();
    if (failures) {
        fprintf(stderr, "C TESTS: FAIL (%d failures)\n", failures);
        return 1;
    }
    printf("C TESTS: ALL PASS\n");
    return 0;
}
