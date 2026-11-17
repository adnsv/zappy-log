// Copyright (c) 2024-2026 Alex Denisov
#include "check.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <thread>
#include <vector>
#include <zappy/logger.hpp>
#include <zappy/sinks/func.hpp>

using namespace std::chrono_literals;
using zappy::level;

// gate blocks threads until opened.
struct gate {
    std::mutex m;
    std::condition_variable cv;
    bool open_ = false;
    void open()
    {
        {
            auto _ = std::lock_guard{m};
            open_ = true;
        }
        cv.notify_all();
    }
    void wait()
    {
        auto l = std::unique_lock{m};
        cv.wait(l, [this] { return open_; });
    }
};

// capture records lines and flushes. The worker calls the sink while the
// test thread reads, so reads go through the mutex.
struct capture {
    std::mutex m;
    std::vector<std::string> lines;
    int flushes = 0;
    std::function<void(std::string_view)> on_line;

    auto sink() -> zappy::sink_ptr
    {
        return zappy::func_sink(
            [this](std::string_view b) {
                if (on_line)
                    on_line(b);
                auto _ = std::lock_guard{m};
                lines.emplace_back(b);
            },
            [this] {
                auto _ = std::lock_guard{m};
                ++flushes;
            });
    }
    auto count() -> std::size_t
    {
        auto _ = std::lock_guard{m};
        return lines.size();
    }
    auto flush_count() -> int
    {
        auto _ = std::lock_guard{m};
        return flushes;
    }
    auto has(std::string_view s) -> bool
    {
        auto _ = std::lock_guard{m};
        return std::any_of(lines.begin(), lines.end(),
            [&](auto const& l) { return l.find(s) != std::string::npos; });
    }
};

static auto wait_until(auto pred, std::chrono::milliseconds limit = 5s) -> bool
{
    auto const end = std::chrono::steady_clock::now() + limit;
    while (!pred()) {
        if (std::chrono::steady_clock::now() > end)
            return false;
        std::this_thread::sleep_for(1ms);
    }
    return true;
}

static void delivers_everything_in_order_per_thread()
{
    auto c = capture{};
    {
        auto core = zappy::make_async_core({c.sink()}, {.capacity = 64});
        auto lg = zappy::logger{"t", core};
        auto threads = std::vector<std::thread>{};
        for (int t = 0; t < 8; ++t)
            threads.emplace_back([&, t] {
                for (int i = 0; i < 2000; ++i)
                    lg.info("m", {{"t", t}, {"i", i}});
            });
        for (auto& t : threads)
            t.join();
    } // ~core shuts down: drain, flush, join
    CHECK_EQ(c.lines.size(), std::size_t{16000});
    auto last = std::map<int, int>{};
    auto in_order = true;
    for (auto const& l : c.lines) {
        auto const t = std::stoi(l.substr(l.find("\"t\":") + 4));
        auto const i = std::stoi(l.substr(l.find("\"i\":") + 4));
        if (last.count(t) && last[t] + 1 != i)
            in_order = false;
        last[t] = i;
    }
    CHECK(in_order);
}

static void info_returns_without_waiting_for_the_sink()
{
    auto g = gate{};
    auto c = capture{};
    c.on_line = [&](std::string_view) { g.wait(); };
    auto core = zappy::make_async_core({c.sink()});
    auto lg = zappy::logger{"t", core};
    lg.info("one"); // the worker blocks in the sink; info() must not
    lg.info("two");
    CHECK_EQ(c.count(), std::size_t{0});
    g.open();
    core->flush();
    CHECK_EQ(c.count(), std::size_t{2});
}

static void error_and_flush_wait_until_written_and_flushed()
{
    auto c = capture{};
    c.on_line = [](std::string_view) { std::this_thread::sleep_for(20ms); };
    auto core = zappy::make_async_core({c.sink()});
    auto lg = zappy::logger{"t", core};
    for (int i = 0; i < 5; ++i)
        lg.info("before");
    lg.error("e"); // flush_on: written and flushed before it returns
    CHECK(c.has(R"("msg":"e")"));
    CHECK_EQ(c.count(), std::size_t{6});
    CHECK(c.flush_count() >= 1);

    lg.info("later");
    core->flush();
    CHECK(c.has(R"("msg":"later")"));
}

static void error_waits_only_for_its_own_message_under_load()
{
    auto c = capture{};
    auto core = zappy::make_async_core({c.sink()}, {.capacity = 256});
    auto lg = zappy::logger{"t", core};
    auto stop = std::atomic<bool>{false};
    auto spam = std::thread([&] {
        while (!stop)
            lg.info("spam");
    });
    auto const t0 = std::chrono::steady_clock::now();
    lg.error("e");
    auto const took = std::chrono::steady_clock::now() - t0;
    stop = true;
    spam.join();
    CHECK(c.has(R"("msg":"e")"));
    CHECK(took < 2s); // used to wait for an empty queue, i.e. forever
}

static void full_queue_blocks_the_producer_until_space()
{
    auto g = gate{};
    auto c = capture{};
    c.on_line = [&](std::string_view) { g.wait(); };
    auto core = zappy::make_async_core({c.sink()}, {.capacity = 2});
    auto lg = zappy::logger{"t", core};
    auto done = std::atomic<int>{0};
    auto producer = std::thread([&] {
        for (int i = 0; i < 10; ++i) {
            lg.info("m");
            ++done;
        }
    });
    std::this_thread::sleep_for(100ms);
    // the worker holds at most two (one batch), the queue two more: the
    // producer must be blocked well before its tenth message
    CHECK(done.load() >= 1 && done.load() <= 4);
    g.open();
    producer.join();
    core->flush();
    CHECK_EQ(c.count(), std::size_t{10});
}

static void flushes_when_idle()
{
    auto c = capture{};
    auto core = zappy::make_async_core({c.sink()});
    zappy::logger{"t", core}.info("m");
    CHECK(wait_until([&] { return c.flush_count() >= 1; }));
}

static void nested_logging_on_the_worker()
{
    auto errors = std::vector<zappy::error_kind>{};
    auto errors_m = std::mutex{};
    auto c = capture{};
    auto weak = std::shared_ptr<zappy::weak_logger>{};
    c.on_line = [&](std::string_view b) {
        if (b.find(R"("msg":"outer")") != std::string_view::npos)
            weak->info("nested");
        if (b.find(R"("msg":"nested")") != std::string_view::npos)
            weak->info("too deep");
    };
    auto core = zappy::make_async_core({c.sink()},
        {.on_error = [&](zappy::error_info const& e) {
            auto _ = std::lock_guard{errors_m};
            errors.push_back(e.kind);
        }});
    auto lg = zappy::logger{"t", core};
    weak = std::make_shared<zappy::weak_logger>(lg.weak());
    lg.info("outer");
    core->flush();
    CHECK(wait_until([&] { return c.has(R"("msg":"nested")"); }));
    core->flush();
    CHECK(!c.has("too deep"));
    auto _ = std::lock_guard{errors_m};
    CHECK_EQ(errors.size(), std::size_t{1});
    CHECK(!errors.empty() && errors[0] == zappy::error_kind::dropped);
}

static void flush_and_error_from_a_sink_do_not_deadlock()
{
    auto c = capture{};
    auto core = std::shared_ptr<zappy::core>{};
    auto weak = std::shared_ptr<zappy::weak_logger>{};
    c.on_line = [&](std::string_view b) {
        if (b.find(R"("msg":"outer")") != std::string_view::npos) {
            weak->error("from sink"); // would wait for itself
            core->flush();            // likewise
        }
    };
    core = zappy::make_async_core({c.sink()});
    auto lg = zappy::logger{"t", core};
    weak = std::make_shared<zappy::weak_logger>(lg.weak());
    lg.info("outer");
    CHECK(wait_until([&] { return c.has("from sink"); }));
    core->shutdown();
    core.reset();
}

static void shutdown_is_idempotent_and_later_logging_is_synchronous()
{
    auto c = capture{};
    auto core = zappy::make_async_core({c.sink()});
    auto lg = zappy::logger{"t", core};
    lg.info("before");
    core->shutdown();
    CHECK_EQ(c.count(), std::size_t{1});
    core->shutdown();
    lg.info("after"); // no worker: written before info() returns
    CHECK(c.has(R"("msg":"after")"));
}

static void last_reference_released_on_the_worker()
{
    auto g = gate{};
    auto released = std::atomic<bool>{false};
    struct holder {
        std::shared_ptr<zappy::logger> lg;
    };
    auto h = std::make_shared<holder>();
    auto core = zappy::make_async_core({zappy::func_sink([&, h](std::string_view) {
        g.wait();
        h->lg.reset(); // the last reference to the core: ~core on the worker
        released = true;
    })});
    h->lg = std::make_shared<zappy::logger>("t", std::move(core));
    h->lg->info("bye");
    g.open();
    // h is shared with the sink lambda; the core is only held by h->lg
    CHECK(wait_until([&] { return released.load(); }));
    std::this_thread::sleep_for(50ms); // the worker finishes and exits
}

static void sink_failures_are_contained_on_the_worker()
{
    auto c = capture{};
    auto failures = std::atomic<int>{0};
    auto core = zappy::make_async_core(
        {zappy::func_sink(
             [](std::string_view) { throw std::runtime_error("boom"); }),
            c.sink()},
        {.on_error = [&](zappy::error_info const&) { ++failures; }});
    auto lg = zappy::logger{"t", core};
    lg.info("one");
    lg.error("two");
    CHECK_EQ(c.count(), std::size_t{2});
    CHECK(failures.load() >= 2);
}

static void manual_mode_is_driven_by_pump()
{
    auto c = capture{};
    auto core = zappy::make_async_core({c.sink()}, {.manual = true});
    auto lg = zappy::logger{"t", core};
    lg.info("one");
    lg.info("two");
    CHECK_EQ(c.count(), std::size_t{0});
    core->pump();
    CHECK_EQ(c.count(), std::size_t{2});
    lg.error("three"); // must not wait for a worker that does not exist
    CHECK_EQ(c.count(), std::size_t{3});
}

int main()
{
    delivers_everything_in_order_per_thread();
    info_returns_without_waiting_for_the_sink();
    error_and_flush_wait_until_written_and_flushed();
    error_waits_only_for_its_own_message_under_load();
    full_queue_blocks_the_producer_until_space();
    flushes_when_idle();
    nested_logging_on_the_worker();
    flush_and_error_from_a_sink_do_not_deadlock();
    shutdown_is_idempotent_and_later_logging_is_synchronous();
    last_reference_released_on_the_worker();
    sink_failures_are_contained_on_the_worker();
    manual_mode_is_driven_by_pump();
    return TEST_MAIN_RESULT();
}
