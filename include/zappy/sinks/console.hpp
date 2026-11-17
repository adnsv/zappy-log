// Copyright (c) 2024-2026 Alex Denisov
#pragma once

#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <zappy/details/common.hpp>
#include <zappy/details/fmt.hpp>
#include <zappy/details/terminal.hpp>

namespace zappy {

namespace details {

struct console_sink_impl : public sink {
    std::string scratch;
    std::ostream& out;
    ansi_fmt fmt;

    console_sink_impl(std::ostream& s, level_filter&& f)
        : sink{std::move(f)}
        , out{s}
        , fmt{is_terminal(s.rdbuf()) && is_color_terminal(s.rdbuf())}
    {
    }

    void prepare_output(std::string& out, msg const& m)
    {
        out.clear();
        fmt.format(out, m);
    }

    void write(msg const& m) override
    {
        prepare_output(scratch, m);
        scratch += "\n";

        // use global mutex in case stdout and stderr point to the same location
        static auto global_mux = std::mutex{};
        auto _ = std::unique_lock(global_mux);
        out.write(scratch.data(), scratch.size());
    }

    void flush() override {
        out.flush();
    }
};

} // namespace details

// Each call returns a new sink with its own filter; writes from all console
// sinks are serialized, so output lines never interleave.
inline auto stdout_sink(level_filter&& f) -> sink_ptr
{
    return std::make_shared<details::console_sink_impl>(std::cout, std::move(f));
}

inline auto stderr_sink(level_filter&& f) -> sink_ptr
{
    return std::make_shared<details::console_sink_impl>(std::cerr, std::move(f));
}

} // namespace zappy
