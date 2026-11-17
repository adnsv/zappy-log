// Copyright (c) 2024-2026 Alex Denisov
#pragma once

// Minimal test harness: CHECK records a failure and continues; main returns
// the failure count so CTest reports it. Failures print the line and the
// values, not the expression: MSVC cannot stringize raw string literals.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace zappy_test {
inline int failures = 0;

inline auto read_file(std::filesystem::path const& p) -> std::string
{
    auto f = std::ifstream{p, std::ios::binary};
    auto ss = std::stringstream{};
    ss << f.rdbuf();
    return ss.str();
}

// a fresh, empty directory under the build tree's working directory
inline auto fresh_dir(std::string const& name) -> std::filesystem::path
{
    auto p = std::filesystem::current_path() / "test-output" / name;
    std::filesystem::remove_all(p);
    std::filesystem::create_directories(p);
    return p;
}
} // namespace zappy_test

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::fprintf(                                                      \
                stderr, "%s:%d: CHECK failed\n", __FILE__, __LINE__);          \
            ++zappy_test::failures;                                            \
        }                                                                      \
    } while (0)

#define CHECK_EQ(a, b)                                                         \
    do {                                                                       \
        auto const& va_ = (a);                                                 \
        auto const& vb_ = (b);                                                 \
        if (!(va_ == vb_)) {                                                   \
            std::ostringstream os_;                                            \
            os_ << __FILE__ << ":" << __LINE__ << ": CHECK_EQ failed"          \
                << "\n  left:  " << va_ << "\n  right: " << vb_ << "\n";        \
            std::fputs(os_.str().c_str(), stderr);                             \
            ++zappy_test::failures;                                            \
        }                                                                      \
    } while (0)

#define TEST_MAIN_RESULT() (zappy_test::failures == 0 ? 0 : 1)
