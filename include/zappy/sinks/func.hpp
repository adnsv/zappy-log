// Copyright (c) 2024-2026 Alex Denisov
#pragma once

#include <functional>
#include <memory>
#include <string_view>
#include <zappy/details/common.hpp>
#include <zappy/details/fmt.hpp>

namespace zappy {

using write_func = std::function<void(std::string_view bytes)>;
using flush_func = std::function<void()>;

namespace details {

struct func_sink_impl final : public sink {
    write_func on_write;
    flush_func on_flush;

    func_sink_impl(write_func&& w, flush_func&& f, level_mask m,
        formatter_ptr fmt)
        : sink{std::move(fmt), m}
        , on_write{std::move(w)}
        , on_flush{std::move(f)}
    {
    }
    void write(std::string_view bytes) override
    {
        if (on_write)
            on_write(bytes);
    }
    void flush() override
    {
        if (on_flush)
            on_flush();
    }
};

} // namespace details

// func_sink calls w with each formatted line (newline included) and f on
// flush. The core serializes the calls, so w and f need no locking.
inline auto func_sink(write_func w, flush_func f = {},
    level_mask m = all_levels, formatter_ptr fmt = json_format()) -> sink_ptr
{
    return std::make_shared<details::func_sink_impl>(
        std::move(w), std::move(f), m, std::move(fmt));
}

} // namespace zappy
