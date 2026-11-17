// Copyright (c) 2024-2026 Alex Denisov
#pragma once

#include <algorithm>
#include <atomic>
#include <mutex>
#include <span>
#include <vector>
#include <zappy/details/common.hpp>
#include <zappy/details/queue.hpp>
#include <zappy/details/worker.hpp>

namespace zappy {

// core provides a thread-save queue for messages which are periodically and
// asynronosly pulled into sinks.
struct core {
private:
    details::queue<msg> mq;
    std::vector<sink_ptr> const sinks;

    inline static std::vector<core*> instances;
    inline static std::mutex sink_mtx_;
    // false before the worker starts and once it is stopping at exit
    inline static std::atomic<bool> worker_running_{false};
    // true while this thread is running sinks; logging from inside a sink
    // must neither block on the queue nor re-enter flush
    inline static thread_local bool in_sink_ = false;
    static void want_thread();
    static void drain(core& c);

public:
    level_filter levels;
    inline static level_filter auto_flush =
        zappy::levels(level::error, level::critical);

    template <typename SinkIter>
    core(std::size_t mq_size, SinkIter begin, SinkIter end);
    ~core();

    auto should_log(level v) const -> bool;

    void write(msg&& m);

    static void flush();
};

inline auto make_core(std::size_t mq_size,
    std::initializer_list<sink_ptr> sinks) -> std::shared_ptr<core>
{
    return std::make_shared<core>(mq_size, sinks.begin(), sinks.end());
}

inline auto make_core(std::size_t mq_size, std::span<sink_ptr> sinks)
    -> std::shared_ptr<core>
{
    return std::make_shared<core>(mq_size, sinks.begin(), sinks.end());
}

template <typename SinkIter>
inline core::core(std::size_t mq_size, SinkIter begin, SinkIter end)
    : mq{mq_size}
    , sinks{begin, end}
{
    want_thread();
    auto _ = std::unique_lock(sink_mtx_);
    instances.push_back(this);
}

inline core::~core()
{
    auto _ = std::unique_lock(sink_mtx_);
    auto it = std::find(instances.begin(), instances.end(), this);
    if (it != instances.end()) {
        instances.erase(it);
        drain(*this);
    }
}

inline auto core::should_log(level v) const -> bool
{
    return !sinks.empty() && (!levels || levels(v));
}

inline void core::write(msg&& m)
{
    if (!should_log(m.level))
        return;

    if (in_sink_) {
        // this thread is the one draining the queue: waiting for room would
        // never end, so drop the message if the queue is full
        mq.try_push(std::move(m));
        return;
    }

    if (worker_running_) {
        mq.push(std::move(m));
        return;
    }

    // no worker (logging during static destruction): drain synchronously
    // instead of waiting for a consumer that will never come
    while (!mq.try_push(std::move(m)))
        flush();
}

// drain delivers everything queued in c and flushes its sinks. The caller
// holds sink_mtx_. A sink that throws loses that message; it cannot take down
// the worker thread or the caller.
inline void core::drain(core& c)
{
    struct in_sink_guard {
        in_sink_guard() { in_sink_ = true; }
        ~in_sink_guard() { in_sink_ = false; }
    } _;

    msg m;
    while (c.mq.try_pop(m))
        for (auto&& s : c.sinks)
            if (s->should_log(m.level)) {
                try {
                    s->write(m);
                }
                catch (...) {
                }
            }

    for (auto&& s : c.sinks) {
        try {
            s->flush();
        }
        catch (...) {
        }
    }
}

inline void core::flush()
{
    if (in_sink_)
        return; // called from a sink: the outer flush is already draining

    auto _ = std::unique_lock(sink_mtx_);
    for (auto& it : instances)
        drain(*it);
}

inline void core::want_thread()
{
    struct worker_holder {
        details::worker w{std::chrono::milliseconds{20}, [] { core::flush(); }};
        worker_holder() { worker_running_ = true; }
        // runs before w is joined: from here on, writers drain synchronously
        ~worker_holder() { worker_running_ = false; }
    };
    static auto t = worker_holder{};
}

} // namespace zappy