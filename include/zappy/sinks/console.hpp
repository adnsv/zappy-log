#pragma once

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
        , fmt{is_terminal(s.rdbuf()) && is_color_terminal()}
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
};

} // namespace details

inline auto stdout_sink(level_filter&& f) -> sink_ptr
{
    static auto v =
        std::make_shared<details::console_sink_impl>(std::cout, std::move(f));
    return v;
}

inline auto stderr_sink(level_filter&& f) -> sink_ptr
{
    static auto v =
        std::make_shared<details::console_sink_impl>(std::cerr, std::move(f));
    return v;
}

} // namespace zappy
