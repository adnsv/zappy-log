// Copyright (c) 2024-2026 Alex Denisov
#include "check.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>
#include <zappy/logger.hpp>
#include <zappy/sinks/best_effort.hpp>
#include <zappy/sinks/func.hpp>

using namespace std::chrono_literals;

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

struct capture {
    std::mutex m;
    std::vector<std::string> lines;
    auto count() -> std::size_t
    {
        auto _ = std::lock_guard{m};
        return lines.size();
    }
    auto snapshot() -> std::vector<std::string>
    {
        auto _ = std::lock_guard{m};
        return lines;
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

static void a_stuck_sink_never_stalls_the_core()
{
    auto g = gate{};
    auto entered = std::atomic<bool>{false};
    auto slow = capture{};
    auto reliable = capture{};
    auto lg = zappy::logger{"t",
        zappy::make_core({
            zappy::best_effort(zappy::func_sink([&](std::string_view b) {
                entered = true;
                g.wait();
                auto _ = std::lock_guard{slow.m};
                slow.lines.emplace_back(b);
            }),
                4),
            zappy::func_sink([&](std::string_view b) {
                reliable.lines.emplace_back(b);
            }),
        })};

    lg.info("first");
    CHECK(wait_until([&] { return entered.load(); })); // stuck in write
    auto const t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 19; ++i)
        lg.info("m", {{"i", i}});
    lg.error("e"); // sync core: written and flushed, but not waiting for slow
    CHECK(std::chrono::steady_clock::now() - t0 < 1s);
    CHECK_EQ(reliable.lines.size(), std::size_t{21});
    CHECK_EQ(slow.count(), std::size_t{0});

    g.open();
    // "first" was in the sink, 4 were buffered, the other 16 dropped; then
    // one "messages dropped" line marks the gap
    CHECK(wait_until([&] { return slow.count() >= 6; }));
    std::this_thread::sleep_for(50ms);
    auto const got = slow.snapshot();
    CHECK_EQ(got.size(), std::size_t{6});
    auto drop_lines = 0;
    for (auto const& l : got)
        if (l.find(R"("logger":"zappy","level":"warn","msg":"messages dropped","dropped":16})") !=
            std::string::npos)
            ++drop_lines;
    CHECK_EQ(drop_lines, 1);
    CHECK(!got.empty() &&
          got.back().find("messages dropped") != std::string::npos);
}

static void a_recovered_sink_gets_everything()
{
    auto out = capture{};
    {
        auto lg = zappy::logger{"t",
            zappy::make_core({zappy::best_effort(
                zappy::func_sink([&](std::string_view b) {
                    auto _ = std::lock_guard{out.m};
                    out.lines.emplace_back(b);
                }))})};
        for (int i = 0; i < 100; ++i)
            lg.info("m");
    } // destroying the sink drains it
    CHECK_EQ(out.count(), std::size_t{100});
}

static void destroying_a_stuck_sink_does_not_hang()
{
    auto g = std::make_shared<gate>();
    auto const t0 = std::chrono::steady_clock::now();
    {
        auto lg = zappy::logger{"t",
            zappy::make_core({zappy::best_effort(
                zappy::func_sink([g](std::string_view) { g->wait(); }))})};
        lg.info("stuck");
    }
    CHECK(std::chrono::steady_clock::now() - t0 < 3s);
    g->open(); // lets the detached thread finish
    std::this_thread::sleep_for(50ms);
}

int main()
{
    a_stuck_sink_never_stalls_the_core();
    a_recovered_sink_gets_everything();
    destroying_a_stuck_sink_does_not_hang();
    return TEST_MAIN_RESULT();
}
