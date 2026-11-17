// Copyright (c) 2024-2026 Alex Denisov
#pragma once

#include <filesystem>
#include <memory>
#include <string_view>
#include <zappy/details/common.hpp>
#include <zappy/details/fmt.hpp>
#include <zappy/details/rotating-file.hpp>

namespace zappy {

struct rotating_file_policy {
    std::size_t max_size = 1024 * 1024;
    std::size_t max_count = 5;
};

namespace details {

struct rotating_file_sink_impl final : public sink {
    details::rotating_file f;

    rotating_file_sink_impl(std::filesystem::path const& fn, formatter_ptr fmt,
        rotating_file_policy const& pol, level_mask m)
        : sink{std::move(fmt), m}
        , f{fn, details::rotating_file::policy{pol.max_size, pol.max_count}}
    {
    }

    void write(std::string_view bytes) override { f.write(bytes); }
    void flush() override { f.flush(); }
};

} // namespace details

// rotating_file_sink writes lines in format fmt to fn, rotating by size.
inline auto rotating_file_sink(std::filesystem::path const& fn,
    formatter_ptr fmt, rotating_file_policy const& pol = {},
    level_mask m = all_levels) -> sink_ptr
{
    return std::make_shared<details::rotating_file_sink_impl>(
        fn, std::move(fmt), pol, m);
}

// rotating_json_file_sink writes zap-compatible JSON lines.
inline auto rotating_json_file_sink(std::filesystem::path const& fn,
    rotating_file_policy const& pol = {}, level_mask m = all_levels)
    -> sink_ptr
{
    return rotating_file_sink(fn, json_format(), pol, m);
}

inline auto rotating_json_file_sink(std::filesystem::path const& fn,
    level_mask m) -> sink_ptr
{
    return rotating_json_file_sink(fn, rotating_file_policy{}, m);
}

// rotating_text_file_sink writes human-readable text lines.
inline auto rotating_text_file_sink(std::filesystem::path const& fn,
    rotating_file_policy const& pol = {}, level_mask m = all_levels)
    -> sink_ptr
{
    return rotating_file_sink(fn, text_format(), pol, m);
}

inline auto rotating_text_file_sink(std::filesystem::path const& fn,
    level_mask m) -> sink_ptr
{
    return rotating_text_file_sink(fn, rotating_file_policy{}, m);
}

} // namespace zappy
