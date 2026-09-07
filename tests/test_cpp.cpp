// C++ binding test: RAII, callbacks, refcounting.
#include <mempressure.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <thread>

static void write_psi(const std::string &path, double some10) {
    std::ofstream fh(path);
    char buf[128];
    snprintf(buf, sizeof buf,
             "some avg10=%.2f avg60=0.00 avg300=0.00 total=0\n"
             "full avg10=0.00 avg60=0.00 avg300=0.00 total=0\n",
             some10);
    fh << buf;
}

static int failures = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);     \
            failures++;                                                        \
        }                                                                      \
    } while (0)

int main() {
    const std::string path = "/tmp/mp_test_cpp.psi";
    write_psi(path, 0.0);

    mp::Config cfg;
    cfg.poll_interval_sec = 0.02;
    cfg.hysteresis = 1;
    cfg.psi_path = path;

    std::atomic<int> hits{0};
    std::atomic<int> last{-1};
    {
        mp::Monitor monitor(cfg);
        monitor.subscribe([&](mp::Level level) {
            last = static_cast<int>(level);
            hits++;
        });
        write_psi(path, 60.0);
        for (int i = 0; i < 400 && last < static_cast<int>(mp::Level::Critical); i++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        CHECK(last == static_cast<int>(mp::Level::Critical));
        CHECK(hits >= 1);
        CHECK(monitor.currentLevel() == mp::Level::Critical);
    }  // RAII: monitor stops here; a second monitor can start afterwards

    try {
        mp::Monitor again(cfg);
        for (int i = 0; i < 400 && again.currentLevel() != mp::Level::Critical; i++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        CHECK(again.currentLevel() == mp::Level::Critical);
    } catch (const std::exception &e) {
        CHECK(false);
        fprintf(stderr, "restart failed: %s\n", e.what());
    }

    if (failures) {
        fprintf(stderr, "CPP TESTS: FAIL (%d)\n", failures);
        return 1;
    }
    printf("CPP TESTS: ALL PASS\n");
    return 0;
}
