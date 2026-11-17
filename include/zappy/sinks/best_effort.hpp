// Copyright (c) 2024-2026 Alex Denisov
#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <zappy/details/common.hpp>
#include <zappy/details/core.hpp>

namespace zappy {

namespace details {

// sink_access lets a wrapper sink lock another sink the way a core does.
struct sink_access {
    static auto mutex(sink& s) -> std::mutex& { return s.mtx_; }
};

// best_effort_state is shared by the wrapper and its thread, so the thread
// can outlive the wrapper when the inner sink is stuck.
struct best_effort_state {
    sink_ptr inner;
    std::size_t capacity;
    error_handler on_error;

    std::mutex m;
    std::condition_variable cv;
    std::deque<std::string> lines;
    std::size_t dropped = 0;
    bool flush_requested = false;
    bool stopping = false;
    bool done = false;

    best_effort_state(sink_ptr s, std::size_t cap, error_handler h)
        : inner{std::move(s)}
        , capacity{cap ? cap : 1}
        , on_error{h ? std::move(h) : default_error_handler()}
    {
    }

    // report runs on this sink's own thread, after the inner sink's lock is
    // released, so a handler that logs cannot deadlock on it.
    void report(error_kind k, std::string const& what) noexcept
    {
        try {
            on_error({k, inner.get(), 1, what});
        }
        catch (...) {
        }
    }

    void write_inner(std::string_view bytes)
    {
        auto failure = std::string{};
        {
            auto _ = std::lock_guard{sink_access::mutex(*inner)};
            try {
                inner->write(bytes);
                return;
            }
            catch (...) {
                failure = exception_text(std::current_exception());
            }
        }
        report(error_kind::sink_write, failure); // the line is lost
    }

    void flush_inner()
    {
        auto failure = std::string{};
        {
            auto _ = std::lock_guard{sink_access::mutex(*inner)};
            try {
                inner->flush();
                return;
            }
            catch (...) {
                failure = exception_text(std::current_exception());
            }
        }
        report(error_kind::sink_flush, failure);
    }

    // dropped_line formats the "messages dropped" notice in the inner sink's
    // own format.
    auto dropped_line(std::size_t n) -> std::string
    {
        auto const attrs = std::initializer_list<attribute>{{"dropped", n}};
        auto out = std::string{};
        try {
            inner->format().format(
                record{
                    .time = clock::now(),
                    .lvl = level::warn,
                    .logger = "zappy",
                    .message = "messages dropped",
                    .bound = {},
                    .attrs = {attrs.begin(), attrs.size()},
                },
                out);
        }
        catch (...) {
            out.clear();
        }
        return out;
    }

    void run()
    {
        auto lk = std::unique_lock{m};
        for (;;) {
            cv.wait(lk, [&] {
                return !lines.empty() || dropped || flush_requested ||
                       stopping;
            });
            if (!lines.empty()) {
                auto line = std::move(lines.front());
                lines.pop_front();
                lk.unlock();
                write_inner(line);
                lk.lock();
                continue;
            }
            // caught up: mark the gap, then flush
            if (auto n = std::exchange(dropped, 0)) {
                lk.unlock();
                write_inner(dropped_line(n));
                lk.lock();
                continue;
            }
            auto const flush = std::exchange(flush_requested, false);
            if (flush) {
                lk.unlock();
                flush_inner();
                lk.lock();
                continue;
            }
            if (stopping)
                break;
        }
        done = true;
        cv.notify_all();
    }
};

struct best_effort_sink final : sink {
    std::shared_ptr<best_effort_state> st;

    best_effort_sink(
        sink_ptr inner, std::size_t capacity, error_handler on_error)
        // share the inner sink's formatter, so the core formats once
        : sink{formatter_ptr{inner, &inner->format()}, inner->levels()}
        , st{std::make_shared<best_effort_state>(
              std::move(inner), capacity, std::move(on_error))}
    {
        std::thread{[s = st] { s->run(); }}.detach();
    }

    ~best_effort_sink() override
    {
        // drain, but never wait long for a stuck sink: after 1 s its thread
        // is left to finish (or not) on its own
        auto lk = std::unique_lock{st->m};
        st->stopping = true;
        st->flush_requested = true;
        st->cv.notify_all();
        st->cv.wait_for(lk, std::chrono::seconds{1}, [&] { return st->done; });
    }

    void write(std::string_view bytes) override
    {
        {
            auto _ = std::lock_guard{st->m};
            if (st->lines.size() >= st->capacity) {
                ++st->dropped;
                return;
            }
            st->lines.emplace_back(bytes);
        }
        st->cv.notify_one();
    }

    void flush() override
    {
        {
            auto _ = std::lock_guard{st->m};
            st->flush_requested = true;
        }
        st->cv.notify_one();
    }
};

} // namespace details

// best_effort wraps a sink so that it can never slow logging down: it gets
// its own thread and a buffer of `capacity` lines. When the buffer is full,
// lines for this sink are dropped and counted, and once the sink catches up
// it writes one "messages dropped" line where the gap is. error() and
// flush() do not wait for it. Failures of the inner sink (a write or flush
// that throws) go to on_error, called from this sink's own thread; the
// default writes one line to stderr, at most once per 10 s per problem.
inline auto best_effort(sink_ptr inner, std::size_t capacity = 1024,
    error_handler on_error = {}) -> sink_ptr
{
    return std::make_shared<details::best_effort_sink>(
        std::move(inner), capacity, std::move(on_error));
}

} // namespace zappy
