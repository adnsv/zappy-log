#pragma once

#include <functional>
#include <zappy/details/common.hpp>

namespace zappy {

using write_func = std::function<void(msg const&)>;
using flush_func = std::function<void()>;

namespace detail {

struct func_sink_impl : public sink {
    write_func on_write;
    flush_func on_flush;

    func_sink_impl(write_func&& w, flush_func&& f)
        : on_write{std::move(w)}
        , on_flush{std::move(f)}
    {
    }
    void write(msg const& m) override
    {
        if (on_write)
            on_write(m);
    }
    void flush() override
    {
        if (on_flush)
            on_flush();
    }
};

} // namespace detail

inline auto func_sink(write_func&& w, flush_func&& f) -> sink_ptr
{
    return std::make_shared<detail::func_sink_impl>(std::move(w), std::move(f));
}

} // namespace zappy