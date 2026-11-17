// Copyright (c) 2024-2026 Alex Denisov
#include <future>
#include <zappy/logger.hpp>
#include <zappy/sinks/best_effort.hpp>
#include <zappy/sinks/console.hpp>
#include <zappy/sinks/file.hpp>

using zappy::level;

auto main() -> int
{
    auto const fn = std::string{"./testdata/logfile"};
    auto const rotation = zappy::rotating_file_policy{
        .max_size = 4096, .max_count = 4};

    // An async core: log calls format on the caller and return; its worker
    // thread writes. make_core({...}) would give a synchronous core with the
    // same logger API.
    auto core = zappy::make_async_core({
        // debug..warn to stdout; best_effort: a stalled console (e.g. a
        // Windows console in selection mode) never slows logging down
        zappy::best_effort(zappy::stdout_sink(
            zappy::levels(level::debug, level::warn))),
        // error and above to stderr
        zappy::stderr_sink(zappy::levels(level::error)),
        // error and above to .errors.jsonl, zap-compatible JSON
        zappy::rotating_json_file_sink(
            fn + ".errors.jsonl", rotation, zappy::levels(level::error)),
        // everything to .jsonl and, as text, to .log
        zappy::rotating_json_file_sink(fn + ".jsonl", rotation),
        zappy::rotating_text_file_sink(fn + ".log", rotation),
    });

    auto app = zappy::logger{"app", core};
    auto com = zappy::logger{"com", core};

    auto a = std::async(std::launch::async, [&] {
        for (int i = 0; i < 10; ++i)
            app.info("tick", {{"key", "value"}, {"counter", i}});
    });
    auto b = std::async(std::launch::async, [&] {
        for (int i = 0; i < 10; ++i)
            com.debug("comm message", {{"bytes", 512 * i}, {"ok", true}});
    });
    auto c = std::async(std::launch::async, [&] {
        auto dev = com.with_attributes({{"device", "UT-4"}, {"channel", 3}});
        for (int i = 0; i < 3; ++i)
            dev.info("acquired", {{"elapsed_ms", 42.5 + i}});
    });
    a.wait();
    b.wait();
    c.wait();

    // error and above are written and flushed before the call returns
    com.error("link lost", {{"retries", 3}});

    core->shutdown(); // also done when the last reference goes away
}
