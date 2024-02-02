#pragma once

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
    static void want_thread();

public:
    level_filter levels;

    template <typename SinkIter>
    core(std::size_t mq_size, SinkIter begin, SinkIter end);
    ~core();

    auto should_log(level v) const -> bool;

    void write(msg&& m);

    static void tick();
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
        msg m;
        while (mq.try_pop(m))
            for (auto&& s : sinks)
                if (s->should_log(m.level))
                    s->write(m);
    }
}

inline auto core::should_log(level v) const -> bool
{
    return !sinks.empty() && (!levels || levels(v));
}

inline void core::write(msg&& m)
{
    if (should_log(m.level))
        mq.push(std::move(m));
}

inline void core::tick()
{
    msg m;
    auto _ = std::unique_lock(sink_mtx_);
    for (auto& it : instances)
        while (it->mq.try_pop(m))
            for (auto&& s : it->sinks)
                if (s->should_log(m.level))
                    s->write(m);
}

inline void core::want_thread()
{
    static bool core_thread_created = false;
    if (core_thread_created)
        return;

    core_thread_created = true;

    static auto t = details::worker{std::chrono::milliseconds{20}, //
        []() {
            // execute core activities
            core::tick();
        }};
}

} // namespace zappy