// Copyright (c) 2024-2026 Alex Denisov
#include "check.hpp"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>
#include <zappy/logger.hpp>
#include <zappy/sinks/console.hpp>
#include <zappy/sinks/func.hpp>

using zappy::level;

// capture collects the lines a func_sink receives. The core calls a sink
// under the sink's own lock, so the lambdas need no locking of their own.
struct capture {
    std::vector<std::string> lines;
    int flushes = 0;

    auto sink(zappy::level_mask m = zappy::all_levels,
        zappy::formatter_ptr f = zappy::json_format()) -> zappy::sink_ptr
    {
        return zappy::func_sink(
            [this](std::string_view b) { lines.emplace_back(b); },
            [this] { ++flushes; }, m, std::move(f));
    }

    auto has(std::string_view s) const -> bool
    {
        for (auto const& l : lines)
            if (l.find(s) != std::string::npos)
                return true;
        return false;
    }
};

// msg_formatter writes just the message and counts calls.
struct msg_formatter : zappy::formatter {
    mutable std::atomic<int> calls{0};
    void format(zappy::record const& r, std::string& out) const override
    {
        ++calls;
        out.assign(r.message);
        out += '\n';
    }
};

static void writes_before_returning_and_flushes()
{
    auto c = capture{};
    auto lg = zappy::logger{"t", zappy::make_core({c.sink()})};
    lg.info("hello", {{"n", 1}});
    CHECK_EQ(c.lines.size(), std::size_t{1});
    CHECK(c.has(R"("logger":"t","level":"info","msg":"hello","n":1})"));
    CHECK(!c.lines.empty() && c.lines[0].back() == '\n');
    CHECK_EQ(c.flushes, 1); // a sync core flushes after every message
}

static void level_masks_filter_at_every_stage()
{
    auto all = capture{};
    auto errors = capture{};
    auto core = zappy::make_core(
        {all.sink(), errors.sink(zappy::levels(level::error))},
        {.levels = zappy::levels(level::info)});
    auto lg = zappy::logger{"t", core, zappy::levels(level::debug, level::warn)};

    lg.debug("d"); // passes the logger, rejected by the core
    lg.info("i");
    lg.error("e"); // rejected by the logger
    CHECK_EQ(all.lines.size(), std::size_t{1});
    CHECK(all.has(R"("msg":"i")"));
    CHECK(errors.lines.empty());

    auto lg2 = zappy::logger{"t2", core};
    lg2.error("e2");
    CHECK(errors.has(R"("msg":"e2")"));
}

static void formats_once_per_format_and_only_when_needed()
{
    auto f = std::make_shared<msg_formatter>();
    auto a = capture{};
    auto b = capture{};
    auto e = capture{};
    auto lg = zappy::logger{"t",
        zappy::make_core({a.sink(zappy::all_levels, f),
            b.sink(zappy::all_levels, f),
            e.sink(zappy::levels(level::error), f)})};
    lg.info("one");
    CHECK_EQ(f->calls.load(), 1); // shared by a and b; e rejects info
    CHECK_EQ(a.lines.size(), std::size_t{1});
    CHECK_EQ(b.lines.size(), std::size_t{1});
    CHECK(e.lines.empty());
}

static void bound_attributes()
{
    auto c = capture{};
    auto base = zappy::logger{"t", zappy::make_core({c.sink()}),
        zappy::levels(level::info)};
    auto lg = base.with_attributes({{"device", "UT-4"}, {"ch", 3}});
    lg.info("m", {{"n", 1}});
    lg.debug("dropped: the copy keeps the mask");
    CHECK_EQ(c.lines.size(), std::size_t{1});
    CHECK(c.has(R"("msg":"m","device":"UT-4","ch":3,"n":1})"));

    auto lg2 = lg.with_attributes({{"extra", true}});
    lg2.info("m2");
    CHECK(c.has(R"("msg":"m2","device":"UT-4","ch":3,"extra":true})"));
}

static void same_order_in_every_sink()
{
    auto a = capture{};
    auto b = capture{};
    auto lg = zappy::logger{"t", zappy::make_core({a.sink(), b.sink()})};
    auto threads = std::vector<std::thread>{};
    for (int t = 0; t < 8; ++t)
        threads.emplace_back([&, t] {
            for (int i = 0; i < 500; ++i)
                lg.info("m", {{"t", t}, {"i", i}});
        });
    for (auto& t : threads)
        t.join();
    CHECK_EQ(a.lines.size(), std::size_t{4000});
    CHECK(a.lines == b.lines);
}

static void nested_logging_is_delivered_after_and_bounded()
{
    auto errors = std::vector<zappy::error_info>{};
    auto lines = std::vector<std::string>{};
    auto lg = std::shared_ptr<zappy::logger>{};
    auto core = zappy::make_core(
        {zappy::func_sink([&](std::string_view b) {
            lines.emplace_back(b);
            if (b.find(R"("msg":"outer")") != std::string_view::npos)
                lg->info("nested"); // set aside, written right after
            if (b.find(R"("msg":"nested")") != std::string_view::npos)
                lg->info("too deep"); // second level: dropped, reported
        })},
        {.on_error = [&](zappy::error_info const& e) { errors.push_back(e); }});
    lg = std::make_shared<zappy::logger>("t", core);
    lg->info("outer");
    CHECK_EQ(lines.size(), std::size_t{2});
    CHECK(lines.size() == 2 && lines[0].find("outer") != std::string::npos);
    CHECK(lines.size() == 2 && lines[1].find("nested") != std::string::npos);
    CHECK_EQ(errors.size(), std::size_t{1});
    CHECK(!errors.empty() && errors[0].kind == zappy::error_kind::dropped);
    lg.reset();
}

static void sink_and_formatter_failures_are_contained()
{
    struct throwing_formatter : zappy::formatter {
        void format(zappy::record const&, std::string&) const override
        {
            throw std::runtime_error("bad format");
        }
    };
    auto errors = std::vector<zappy::error_kind>{};
    auto ok = capture{};
    auto lg = zappy::logger{"t",
        zappy::make_core(
            {
                zappy::func_sink(
                    [](std::string_view) { throw std::runtime_error("w"); },
                    [] { throw std::runtime_error("f"); }),
                zappy::func_sink([](std::string_view) {}, {}, zappy::all_levels,
                    std::make_shared<throwing_formatter>()),
                ok.sink(),
            },
            {.on_error =
                    [&](zappy::error_info const& e) {
                        errors.push_back(e.kind);
                    }})};
    auto threw = false;
    try {
        lg.info("one");
        lg.error("two");
    }
    catch (...) {
        threw = true;
    }
    CHECK(!threw);
    CHECK_EQ(ok.lines.size(), std::size_t{2});
    auto count = [&](zappy::error_kind k) {
        return std::count(errors.begin(), errors.end(), k);
    };
    CHECK_EQ(count(zappy::error_kind::sink_write), 2);
    CHECK_EQ(count(zappy::error_kind::sink_flush), 2);
    CHECK_EQ(count(zappy::error_kind::format), 2);
}

static void explicit_flush()
{
    auto c = capture{};
    auto core = zappy::make_core({c.sink()});
    core->flush();
    CHECK_EQ(c.flushes, 1);
}

static void weak_logger_does_not_keep_the_core_alive()
{
    auto c = capture{};
    auto core = zappy::make_core({c.sink()});
    auto weak = zappy::logger{"t", core}.weak();
    weak.info("alive");
    CHECK_EQ(c.lines.size(), std::size_t{1});
    core.reset();
    weak.info("gone"); // no-op, no crash
    CHECK_EQ(c.lines.size(), std::size_t{1});
}

static void console_sinks_keep_their_own_filter()
{
    auto a = zappy::stdout_sink(zappy::levels(level::error));
    auto b = zappy::stdout_sink(zappy::levels(level::debug, level::debug));
    CHECK(a != b);
    CHECK(b->levels().contains(level::debug));
    CHECK(!a->levels().contains(level::debug));
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
    writes_before_returning_and_flushes();
    level_masks_filter_at_every_stage();
    formats_once_per_format_and_only_when_needed();
    bound_attributes();
    same_order_in_every_sink();
    nested_logging_is_delivered_after_and_bounded();
    sink_and_formatter_failures_are_contained();
    explicit_flush();
    weak_logger_does_not_keep_the_core_alive();
    console_sinks_keep_their_own_filter();
    color_detection();
    return TEST_MAIN_RESULT();
}
