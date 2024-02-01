#pragma once

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <thread>

namespace zappy::details {

struct rotating_file {
public:
    struct policy {
        std::size_t max_size;
        std::size_t max_count;
    };

private:
    policy policy_;
    std::string fn_base;
    std::string fn_ext;

    std::size_t file_size = 0;

    std::ofstream strm;

    void open(bool truncate);
    void reopen(bool truncate);
    void rotate();
    void close();
    auto make_fn(std::size_t number = 0) -> std::filesystem::path;

public:
    rotating_file(std::filesystem::path const& fn, policy const& p);
    rotating_file(rotating_file const&) = delete;
    rotating_file(rotating_file&&);
    ~rotating_file();

    void write(std::string_view sv);
    void flush();
};

inline void decompose_fn(
    std::string const& fn, std::string& base, std::string& ext)
{
    if (auto p = fn.find_last_of('.');
        p != std::string::npos && p != 0 && p != fn.size() - 1) {
        base = fn.substr(0, p);
        ext = fn.substr(p);
    }
    else {
        base = fn;
        ext = {};
    }
}

inline auto rename_file(std::filesystem::path const& src_fn,
    std::filesystem::path const& target_fn) -> bool
{
    std::filesystem::remove(target_fn);
    auto ec = std::error_code{};
    std::filesystem::rename(src_fn, target_fn, ec);
    return ec == std::error_code();
}

inline rotating_file::rotating_file(
    std::filesystem::path const& fn, policy const& p)
    : policy_{p}
{
    decompose_fn(fn.string(), fn_base, fn_ext);
    open(false);
}

inline rotating_file::rotating_file(rotating_file&& f)
    : policy_{f.policy_}
    , fn_base{f.fn_base}
    , fn_ext{f.fn_ext}
    , file_size{f.file_size}
    , strm{std::move(f.strm)}
{
    f.file_size = 0;
}

inline rotating_file::~rotating_file() { close(); }

inline void rotating_file::write(std::string_view sv)
{
    if (sv.empty())
        return;

    auto const sz = sv.size();

    if (policy_.max_count && file_size + sz > policy_.max_size &&
        file_size > 0) {
        strm.flush();
        rotate();
    }

    if (strm.is_open()) {
        strm.write(sv.data(), sz);
        file_size += sz;
    }
}

inline void rotating_file::flush()
{
    strm.flush();
}

inline void rotating_file::close()
{
    file_size = 0;
    strm.close();
}

inline auto rotating_file::make_fn(std::size_t number) -> std::filesystem::path
{
    auto fn = fn_base;
    if (number) {
        fn += '.';
        fn += std::to_string(number);
    }
    fn += fn_ext;
    return fn;
}

inline void rotating_file::open(bool truncate)
{
    close();

    auto const open_tries = std::size_t{5};
    auto const open_interval = std::chrono::milliseconds(50);

    auto const fn = make_fn();

    for (int tries = 0; tries < open_tries; ++tries) {
        auto ec = std::error_code{};
        std::filesystem::create_directories(fn.parent_path(), ec);
        if (ec != std::error_code()) {
            std::this_thread::sleep_for(open_interval);
            continue;
        }

        if (truncate) {
            auto tmp = std::ofstream{fn, std::ios::out | std::ios::binary};
            if (!tmp.is_open()) {
                std::this_thread::sleep_for(open_interval);
                continue;
            }
            tmp.close();
        }

        strm.open(fn, std::ios::out | std::ios::app | std::ios::binary);
        if (!strm.is_open()) {
            std::this_thread::sleep_for(open_interval);
            continue;
        }

        file_size = std::filesystem::file_size(fn);
        return;
    }
}

inline void rotating_file::reopen(bool truncate) { open(true); }

inline void rotating_file::rotate()
{
    if (policy_.max_count < 2)
        return;

    close();
    for (auto i = policy_.max_count - 1; i > 0; --i) {
        auto src = make_fn(i - 1);
        if (!std::filesystem::exists(src))
            continue;

        auto target = make_fn(i);
        if (!rename_file(src, target)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (!rename_file(src, target)) {
                reopen(true);
            }
        }
    }
    reopen(true);
}

} // namespace zappy::details