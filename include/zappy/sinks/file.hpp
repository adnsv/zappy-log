#pragma once

#include <filesystem>
#include <mutex>
#include <zappy/details/common.hpp>
#include <zappy/details/fmt.hpp>
#include <zappy/details/rotating-file.hpp>
#include <variant>

namespace zappy {

struct rotating_file_policy {
    std::size_t max_size = 1024 * 1024;
    std::size_t max_count = 5;
};

namespace details {

struct json_fmt {};
struct text_fmt {};
using fmt = std::variant<json_fmt, text_fmt>;

struct rotating_file_sink_impl : public sink {
    details::rotating_file f;
    fmt formatter;
    std::mutex write_mux;
    std::string scratch;

    rotating_file_sink_impl(std::filesystem::path const& fn,
        fmt const& formatter, rotating_file_policy const& pol,
        level_filter&& flt)
        : sink{std::move(flt)}
        , f{fn, details::rotating_file::policy{pol.max_size, pol.max_count}}
        , formatter{formatter}
    {
    }

    void prepare_output(std::string& out, msg const& m)
    {
        if (std::holds_alternative<json_fmt>(formatter))
            to_json(out, m);
        else if (std::holds_alternative<text_fmt>(formatter))
            to_text(out, m);
    }

    void write(msg const& m) override
    {
        auto _ = std::unique_lock(write_mux);
        prepare_output(scratch, m);
        scratch += "\n";
        f.write(scratch);
    }
};

} // namespace details

inline auto rotating_json_file_sink(std::filesystem::path const& fn,
    rotating_file_policy const& pol = {}, level_filter&& flt = {}) -> sink_ptr
{
    return std::make_shared<details::rotating_file_sink_impl>(
        fn, details::json_fmt{}, pol, std::move(flt));
}

inline auto rotating_json_file_sink(
    std::filesystem::path const& fn, level_filter&& flt) -> sink_ptr
{
    return rotating_json_file_sink(fn, rotating_file_policy{}, std::move(flt));
}

inline auto rotating_text_file_sink(std::filesystem::path const& fn,
    rotating_file_policy const& pol = {}, level_filter&& flt = {}) -> sink_ptr
{
    return std::make_shared<details::rotating_file_sink_impl>(
        fn, details::text_fmt{}, pol, std::move(flt));
}

inline auto rotating_text_file_sink(
    std::filesystem::path const& fn, level_filter&& flt) -> sink_ptr
{
    return rotating_text_file_sink(fn, rotating_file_policy{}, std::move(flt));
}

} // namespace zappy