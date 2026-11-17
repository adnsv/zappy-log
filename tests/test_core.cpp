#include "check.hpp"

#include <atomic>
#include <stdexcept>
#include <zappy/logger.hpp>
#include <zappy/sinks/console.hpp>
#include <zappy/sinks/func.hpp>

using zappy::level;

static void throwing_sink_is_contained()
{
    auto delivered = std::atomic<int>{0};
    auto threw = false;
    {
        auto core = zappy::make_core(8,
            {
                zappy::func_sink(
                    [](zappy::msg const&) { throw std::runtime_error("boom"); },
                    [] { throw std::runtime_error("boom"); }),
                zappy::func_sink([&](zappy::msg const&) { ++delivered; }, {}),
            });
        auto lg = zappy::logger{"t", core};
        try {
            lg.info("one");
            lg.error("two"); // auto-flush runs the sinks on this thread
            lg.info("three");
        }
        catch (...) {
            threw = true;
        }
    }
    CHECK(!threw);
    CHECK_EQ(delivered.load(), 3);
}

// A sink that logs an error re-enters auto-flush from inside flush.
static void sink_logging_error_does_not_deadlock()
{
    auto core = std::shared_ptr<zappy::core>{};
    auto inner = std::shared_ptr<zappy::logger>{};
    auto seen = std::atomic<int>{0};
    core = zappy::make_core(8, {zappy::func_sink(
                                   [&](zappy::msg const& m) {
                                       ++seen;
                                       if (m.logger_name == "outer")
                                           inner->error("from sink");
                                   },
                                   {})});
    inner = std::make_shared<zappy::logger>("inner", core);
    zappy::logger{"outer", core}.error("trigger");
    zappy::core::flush();
    inner.reset();
    core.reset();
    CHECK_EQ(seen.load(), 2);
}

// A sink that logs while producers keep the queue full must not block the
// only thread that drains it.
static void sink_logging_with_full_queue_does_not_deadlock()
{
    auto core = std::shared_ptr<zappy::core>{};
    auto inner = std::shared_ptr<zappy::logger>{};
    core = zappy::make_core(4, {zappy::func_sink(
                                   [&](zappy::msg const& m) {
                                       if (m.logger_name == "outer")
                                           inner->info("echo");
                                   },
                                   {})});
    inner = std::make_shared<zappy::logger>("inner", core);
    auto outer = zappy::logger{"outer", core};
    for (int i = 0; i < 500; ++i)
        outer.info("spam");
    zappy::core::flush(); // no "outer" message left for the sink to echo
    inner.reset();
    core.reset();
    CHECK(true); // reaching here is the test; CTest's timeout catches a hang
}

static void destroying_core_flushes_sinks()
{
    auto events = std::string{};
    auto mtx = std::mutex{};
    {
        auto core = zappy::make_core(8,
            {zappy::func_sink(
                [&](zappy::msg const&) {
                    auto _ = std::lock_guard{mtx};
                    events += 'w';
                },
                [&] {
                    auto _ = std::lock_guard{mtx};
                    events += 'f';
                })});
        zappy::logger{"t", core}.info("last words");
    }
    CHECK(!events.empty() && events.back() == 'f');
}

static void console_sinks_keep_their_own_filter()
{
    auto a = zappy::stdout_sink(zappy::levels(level::error));
    auto b = zappy::stdout_sink(zappy::levels(level::debug, level::debug));
    CHECK(a != b);
    CHECK(b->should_log(level::debug));
    CHECK(!a->should_log(level::debug));
}

static void color_detection()
{
    using zappy::details::color_from_env;
    CHECK(!color_from_env("1", "truecolor", "xterm-256color")); // NO_COLOR wins
    CHECK(color_from_env(nullptr, "truecolor", "foo"));
    CHECK(!color_from_env(nullptr, nullptr, "dumb"));
    CHECK(!color_from_env(nullptr, nullptr, nullptr));
    CHECK(color_from_env(nullptr, nullptr, "xterm-256color"));
    CHECK(color_from_env("", nullptr, "xterm")); // empty NO_COLOR is ignored
}

int main()
{
    throwing_sink_is_contained();
    sink_logging_error_does_not_deadlock();
    sink_logging_with_full_queue_does_not_deadlock();
    destroying_core_flushes_sinks();
    console_sinks_keep_their_own_filter();
    color_detection();
    return TEST_MAIN_RESULT();
}
