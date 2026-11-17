// Copyright (c) 2024-2026 Alex Denisov
#pragma once

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

namespace zappy::details {

// rotating_file writes to dir/stem.ext and, once the file would exceed
// max_size, shifts it to dir/stem.1.ext, stem.1 to stem.2, and so on, keeping
// at most max_count files. max_count 1 truncates the file in place; 0 never
// rotates.
//
// Failures never throw and never lose data already written: a file that cannot
// be opened is retried on a later write, and a rotation that cannot rename a
// file is abandoned (the live file keeps growing) and retried later.
struct rotating_file {
public:
    struct policy {
        std::size_t max_size;
        std::size_t max_count;
    };

private:
    using steady = std::chrono::steady_clock;
    static constexpr auto retry_interval = std::chrono::seconds{1};

    policy policy_;
    std::filesystem::path dir_;
    std::filesystem::path stem_;
    std::filesystem::path ext_;

    std::size_t file_size = 0;
    std::ofstream strm;
    steady::time_point next_open_attempt;
    steady::time_point next_rotate_attempt;

    void open(bool truncate);
    void rotate();
    void close();
    auto make_fn(std::size_t number = 0) const -> std::filesystem::path;

public:
    rotating_file(std::filesystem::path const& fn, policy const& p);
    rotating_file(rotating_file const&) = delete;
    rotating_file(rotating_file&&);
    ~rotating_file();

    void write(std::string_view sv);
    void flush();
};

// rename_file moves src to target, replacing target. It returns false, without
// throwing, if target cannot be removed or src cannot be renamed.
inline auto rename_file(std::filesystem::path const& src_fn,
    std::filesystem::path const& target_fn) -> bool
{
    auto ec = std::error_code{};
    std::filesystem::remove(target_fn, ec);
    if (ec)
        return false;
    std::filesystem::rename(src_fn, target_fn, ec);
    return !ec;
}

inline rotating_file::rotating_file(
    std::filesystem::path const& fn, policy const& p)
    : policy_{p}
{
    // resolve once, so a later change of the working directory does not move
    // the log
    auto ec = std::error_code{};
    auto abs = std::filesystem::absolute(fn, ec);
    if (ec)
        abs = fn;
    dir_ = abs.parent_path();
    stem_ = abs.stem();
    ext_ = abs.extension();
    open(false);
}

inline rotating_file::rotating_file(rotating_file&& f)
    : policy_{f.policy_}
    , dir_{std::move(f.dir_)}
    , stem_{std::move(f.stem_)}
    , ext_{std::move(f.ext_)}
    , file_size{f.file_size}
    , strm{std::move(f.strm)}
    , next_open_attempt{f.next_open_attempt}
    , next_rotate_attempt{f.next_rotate_attempt}
{
    f.file_size = 0;
}

inline rotating_file::~rotating_file() { close(); }

inline void rotating_file::write(std::string_view sv)
{
    if (sv.empty())
        return;

    if (!strm.is_open()) {
        if (steady::now() < next_open_attempt)
            return; // dropped: the file is unavailable
        open(false);
        if (!strm.is_open())
            return;
    }

    auto const sz = sv.size();

    if (policy_.max_count && file_size > 0 &&
        file_size + sz > policy_.max_size &&
        steady::now() >= next_rotate_attempt) {
        rotate();
        if (!strm.is_open())
            return;
    }

    strm.write(sv.data(), sz);
    if (!strm) {
        // e.g. disk full: reopen on a later write rather than write into a
        // stream stuck in a failed state
        close();
        next_open_attempt = steady::now() + retry_interval;
        return;
    }
    file_size += sz;
}

inline void rotating_file::flush()
{
    if (strm.is_open() && !strm.flush()) {
        close();
        next_open_attempt = steady::now() + retry_interval;
    }
}

inline void rotating_file::close()
{
    file_size = 0;
    strm.close();
}

inline auto rotating_file::make_fn(std::size_t number) const
    -> std::filesystem::path
{
    auto name = stem_;
    if (number) {
        name += ".";
        name += std::to_string(number);
    }
    name += ext_;
    return dir_ / name;
}

inline void rotating_file::open(bool truncate)
{
    close();

    auto ec = std::error_code{};
    if (!dir_.empty())
        std::filesystem::create_directories(dir_, ec);

    auto const fn = make_fn();
    strm.open(fn, std::ios::out | std::ios::binary |
                      (truncate ? std::ios::trunc : std::ios::app));
    if (!strm.is_open()) {
        strm.clear();
        next_open_attempt = steady::now() + retry_interval;
        return;
    }

    auto const sz = std::filesystem::file_size(fn, ec);
    file_size = ec ? 0 : std::size_t(sz);
}

inline void rotating_file::rotate()
{
    if (policy_.max_count == 1) {
        open(true);
        return;
    }

    close();

    // shift oldest first, so a failure part way leaves every file in place
    for (auto i = policy_.max_count - 1; i > 0; --i) {
        auto const src = make_fn(i - 1);
        auto ec = std::error_code{};
        if (!std::filesystem::exists(src, ec))
            continue;

        if (!rename_file(src, make_fn(i))) {
            // e.g. on Windows another process holds the file open; keep
            // appending to the live file and try again later
            next_rotate_attempt = steady::now() + retry_interval;
            break;
        }
    }

    open(false);
}

} // namespace zappy::details
