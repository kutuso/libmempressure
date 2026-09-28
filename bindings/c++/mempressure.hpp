// Header-only C++ binding for libmempressure: RAII ownership of the monitor
// and std::function callbacks instead of C function pointers.
//
// The underlying C library is process-global, so Monitors are reference
// counted: the first one starts the thread, the last one stops it.
#ifndef MEMPRESSURE_HPP
#define MEMPRESSURE_HPP

#include <mempressure.h>

#include <atomic>
#include <functional>
#include <stdexcept>
#include <string>
#include <utility>
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
        if (rc != 0 && rc != -EALREADY) {
            throw std::runtime_error("mp_init failed: " + std::to_string(rc));
        }
        refcount()++;
    }

    void stop() {
        if (refcount().load() > 0 && refcount().fetch_sub(1) == 1) {
            mp_shutdown();
        }
    }

    [[nodiscard]] Level currentLevel() const {
        return static_cast<Level>(mp_current_level());
    }

    // Callbacks fire from the monitor thread on level changes; keep them fast.
    // Exceptions escaping a callback are swallowed on the monitor thread (the
    // C boundary would otherwise terminate the process).
    void subscribe(Callback callback) {
        auto *box = new Callback(std::move(callback));
        int handle = mp_subscribe(
            [](mp_level_t level, void *userdata) {
                auto *cb = static_cast<Callback *>(userdata);
                try {
                    (*cb)(static_cast<Level>(level));
                } catch (...) {
                }
            },
            box);
        if (handle < 0) {
            delete box;
            throw std::runtime_error("mp_subscribe failed: " + std::to_string(handle));
        }
        try {
            subs_.push_back({box, handle});
        } catch (...) {
            mp_unsubscribe(handle);
            delete box;
            throw;
        }
    }

    void unsubscribeAll() {
        size_t keep = 0;
        for (size_t i = 0; i < subs_.size(); i++) {
            int rc = mp_unsubscribe(subs_[i].handle);
            if (rc == 0 || rc == -ENOENT) {
                delete subs_[i].box;
            } else {
                subs_[keep++] = subs_[i];
            }
        }
        subs_.resize(keep);
    }

  private:
    static std::atomic<int> &refcount() {
        static std::atomic<int> count{0};
        return count;
    }

    struct Sub {
        Callback *box;
        int handle;
    };

    std::vector<Sub> subs_;
};

}  // namespace mp
#endif
