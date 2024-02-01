#pragma once

#include <functional>
#include <mutex>
#include <thread>

namespace zappy::details {
struct worker {
private:
    bool active;
    std::thread t;
    std::mutex mux;
    std::condition_variable cv;

public:
    worker(std::chrono::milliseconds interval,
        std::function<void()> const& callback)
    {
        active = interval > std::chrono::milliseconds::zero();
        if (!active)
            return;
        t = std::thread([this, callback, interval]() {
            while (true) {
                auto lock = std::unique_lock(this->mux);
                if (this->cv.wait_for(
                        lock, interval, [this] { return !this->active; })) {
                    return;
                }
                callback();
            }
        });
    }

    ~worker()
    {
        if (t.joinable()) {
            {
                auto _ = std::lock_guard{mux};
                active = false;
            }
            cv.notify_one();
            t.join();
        }
    }
};
} // namespace zappy::details