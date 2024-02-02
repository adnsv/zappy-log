#include <future>
#include <zappy/logger.hpp>
#include <zappy/sinks/console.hpp>
#include <zappy/sinks/file.hpp>

auto main() -> int
{

    auto fn = std::string{"./testdata/logfile"};
    auto core = zappy::make_core(64, // message queue capacity
        {
            // push debug..warn messages to stdout
            zappy::stdout_sink(
                zappy::levels(zappy::level::debug, zappy::level::warn)),
            // push error messages to stderr
            zappy::stderr_sink(
                zappy::levels(zappy::level::error, zappy::level::error)),
            // push error messages to .errors.jsonl in json format
            zappy::rotating_json_file_sink(fn + ".errors.jsonl",
                {.max_size = 4096, .max_count = 4},
                zappy::levels(zappy::level::error, zappy::level::error)),
            // push all messages to .jsonl in json format
            zappy::rotating_json_file_sink(
                fn + ".jsonl", {.max_size = 4096, .max_count = 4}),
            // push all messages to .log in text format
            zappy::rotating_text_file_sink(
                fn + ".log", {.max_size = 4096, .max_count = 4}),
        });

    auto APP = zappy::logger{"APP", core};
    auto COM = zappy::logger{"COM", core};

    auto a = std::async(std::launch::async, [&] {
        for (int i = 0; i < 10; ++i) {
            APP.log(zappy::level::info, "blah",
                {{"key", "value"}, {"counter", std::to_string(i)}});
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
    });
    auto b = std::async(std::launch::async, [&] {
        for (int i = 0; i < 10; ++i) {
            COM.log(zappy::level::debug, "comm message",
                {{"key1", "value1"}, {"counter", std::to_string(i)}});
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    });

    auto c = std::async(std::launch::async, [&] {
        for (int i = 0; i < 3; ++i) {
            std::this_thread::sleep_for(std::chrono::microseconds(200));
            auto COM_ATTR = COM.with_attributes({{"k1", "v1"}, {"k2", "v2"}});
            COM_ATTR.info("test message from COM_ATTR logger", {{"k3", "v3"}});
        }
    });

    a.wait();
    b.wait();
    c.wait();

    COM.log(zappy::level::error, "error message #1");
    COM.log(zappy::level::error, "error message #2");
}