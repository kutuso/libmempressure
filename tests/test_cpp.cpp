// C++ binding test: RAII, callbacks, refcounting.
#include <mempressure.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <stdexcept>
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

    {
        mp::Monitor shared(cfg);
        mp::Monitor second(cfg);  // shares the singleton instead of throwing -EALREADY
        std::atomic<int> hits{0};
        second.subscribe([&](mp::Level) {
            hits++;
            throw std::runtime_error("boom");  // must not terminate the process
        });
        std::atomic<int> clean_hits{0};
        shared.subscribe([&](mp::Level) { clean_hits++; });
        write_psi(path, 60.0);
        for (int i = 0; i < 400 && (hits < 1 || clean_hits < 1); i++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        CHECK(hits >= 1);
        CHECK(clean_hits >= 1);  // delivery continues past a throwing callback
    }

    if (failures) {
        fprintf(stderr, "CPP TESTS: FAIL (%d)\n", failures);
        return 1;
    }
    printf("CPP TESTS: ALL PASS\n");
    return 0;
}
