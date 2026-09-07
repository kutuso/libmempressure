// Minimal libmempressure usage from C++: RAII + std::function.
#include <mempressure.hpp>

#include <iostream>
#include <thread>

int main() {
    mp::Config cfg;
    cfg.poll_interval_sec = 1.0;
    mp::Monitor monitor(cfg);
    monitor.subscribe([](mp::Level level) {
        std::cout << "pressure: " << mp::levelName(level) << std::endl;
    });
    std::cout << "watching pressure with libmempressure " << mp_version()
              << " (ctrl-c to stop)" << std::endl;
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(3600));
    }
}
