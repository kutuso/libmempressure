// Header-only C++ binding for libmempressure: RAII ownership of the monitor
// and std::function callbacks instead of C function pointers.
//
// The underlying C library is process-global, so Monitors are reference
// counted: the first one starts the thread, the last one stops it.
#ifndef MEMPRESSURE_HPP
#define MEMPRESSURE_HPP

#include <mempressure.h>

#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace mp {

enum class Level : int {
    None = MP_LEVEL_NONE,
    Low = MP_LEVEL_LOW,
    Moderate = MP_LEVEL_MODERATE,
    Critical = MP_LEVEL_CRITICAL,
};

inline const char *levelName(Level level) {
    return mp_level_name(static_cast<mp_level_t>(level));
}

struct Config {
    double low_threshold = 5.0;
    double moderate_threshold = 15.0;
    double critical_threshold = 40.0;
    double poll_interval_sec = 0.5;
    int hysteresis = 2;
    std::string psi_path;
};

inline mp_config_t to_c(const Config &cfg) {
    mp_config_t out = {};
    out.low_threshold = cfg.low_threshold;
    out.moderate_threshold = cfg.moderate_threshold;
    out.critical_threshold = cfg.critical_threshold;
    out.poll_interval_sec = cfg.poll_interval_sec;
    out.hysteresis = cfg.hysteresis;
    out.psi_path = cfg.psi_path.empty() ? nullptr : cfg.psi_path.c_str();
    return out;
}

class Monitor {
  public:
    using Callback = std::function<void(Level)>;

    explicit Monitor(const Config &cfg = Config()) { start(cfg); }

    ~Monitor() {
        unsubscribeAll();
        stop();
    }

    Monitor(const Monitor &) = delete;
    Monitor &operator=(const Monitor &) = delete;
    Monitor(Monitor &&) = delete;
    Monitor &operator=(Monitor &&) = delete;

    void start(const Config &cfg = Config()) {
        mp_config_t c = to_c(cfg);
        int rc = mp_init(&c);
        if (rc != 0) {
            throw std::runtime_error("mp_init failed: " + std::to_string(rc));
        }
        refcount()++;
    }

    void stop() {
        if (refcount() > 0 && --refcount() == 0) {
            mp_shutdown();
        }
    }

    [[nodiscard]] Level currentLevel() const {
        return static_cast<Level>(mp_current_level());
    }

    // Callbacks fire from the monitor thread on level changes; keep them fast.
    void subscribe(Callback callback) {
        auto *box = new Callback(std::move(callback));
        int handle = mp_subscribe(
            [](mp_level_t level, void *userdata) {
                auto *cb = static_cast<Callback *>(userdata);
                (*cb)(static_cast<Level>(level));
            },
            box);
        if (handle < 0) {
            delete box;
            throw std::runtime_error("mp_subscribe failed: " + std::to_string(handle));
        }
        boxes_.push_back(box);
        ids_.push_back(handle);
    }

    void unsubscribeAll() {
        for (size_t i = 0; i < ids_.size(); i++) {
            mp_unsubscribe(ids_[i]);
            delete static_cast<Callback *>(boxes_[i]);
        }
        boxes_.clear();
        ids_.clear();
    }

  private:
    static int &refcount() {
        static int count = 0;
        return count;
    }

    std::vector<Callback *> boxes_;
    std::vector<int> ids_;
};

}  // namespace mp
#endif
