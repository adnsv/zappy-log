// Copyright (c) 2024-2026 Alex Denisov
#pragma once

#include <iostream>
#include <memory>
#include <mutex>
#include <string_view>
#include <zappy/details/common.hpp>
#include <zappy/details/fmt.hpp>
#include <zappy/details/terminal.hpp>

namespace zappy {

namespace details {

struct console_sink_impl final : public sink {
    std::ostream& out;

    console_sink_impl(std::ostream& s, level_mask m)
        : sink{ansi_format(is_terminal(s.rdbuf()) &&
                           is_color_terminal(s.rdbuf())),
              m}
        , out{s}
    {
    }

    // one lock for all console sinks: stdout and stderr often end up in the
    // same place, and their lines must not interleave
    static auto console_mutex() -> std::mutex&
    {
        static auto m = std::mutex{};
        return m;
    }

    void write(std::string_view bytes) override
    {
        auto _ = std::lock_guard{console_mutex()};
        out.write(bytes.data(), std::streamsize(bytes.size()));
    }

    void flush() override
    {
        auto _ = std::lock_guard{console_mutex()};
        out.flush();
    }
};

} // namespace details

// stdout_sink and stderr_sink write colored text when the stream is a
// terminal that supports it. Each call returns a new sink with its own mask.
inline auto stdout_sink(level_mask m = all_levels) -> sink_ptr
{
    return std::make_shared<details::console_sink_impl>(std::cout, m);
}

inline auto stderr_sink(level_mask m = all_levels) -> sink_ptr
{
    return std::make_shared<details::console_sink_impl>(std::cerr, m);
}

} // namespace zappy
