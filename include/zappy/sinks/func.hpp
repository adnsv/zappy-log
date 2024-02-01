#pragma once

#include <functional>
#include <zappy/details/common.hpp>

namespace zappy {

namespace detail {

struct func_sink_impl : public sink {
    using functor_type = std::function<void(msg const&)>;
    functor_type on_write;
    func_sink_impl(functor_type&& f)
        : on_write{std::move(f)}
    {
    }
    void write(msg const& m) override
    {
        if (on_write)
            on_write(m);
    }
};

} // namespace detail

inline auto func_sink(std::function<void(msg const&)>&& flt) -> sink_ptr
{
    return std::make_shared<detail::func_sink_impl>(std::move(flt));
}

} // namespace zappy