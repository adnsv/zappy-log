#include "check.hpp"

#include <thread>
#include <zappy/logger.hpp>
#include <zappy/sinks/file.hpp>

namespace fs = std::filesystem;
using zappy::details::rotating_file;
using zappy_test::fresh_dir;
using zappy_test::read_file;

// concatenated content of base, base.1, base.2, ... (oldest last)
static auto all_content(fs::path const& dir) -> std::string
{
    auto s = std::string{};
    for (auto const& e : fs::directory_iterator(dir))
        if (e.is_regular_file())
            s += read_file(e.path());
    return s;
}

static void bare_filename()
{
    auto dir = fresh_dir("bare");
    auto const saved = fs::current_path();
    fs::current_path(dir);
    {
        auto core = zappy::make_core(8, {zappy::rotating_json_file_sink("bare.jsonl")});
        zappy::logger{"t", core}.info("hello");
    }
    fs::current_path(saved);
    CHECK(read_file(dir / "bare.jsonl").find("hello") != std::string::npos);
}

static void dot_in_directory_name()
{
    auto dir = fresh_dir("dotdir") / "logs.d";
    {
        auto f = rotating_file{dir / "app", {.max_size = 10, .max_count = 3}};
        f.write("line-1\n");
        f.write("line-2\n");
        f.write("line-3\n");
    }
    CHECK_EQ(read_file(dir / "app"), "line-3\n");
    CHECK_EQ(read_file(dir / "app.1"), "line-2\n");
    CHECK_EQ(read_file(dir / "app.2"), "line-1\n");
}

static void extension_is_kept_after_number()
{
    auto dir = fresh_dir("ext");
    {
        auto f = rotating_file{dir / "app.log", {.max_size = 10, .max_count = 2}};
        f.write("line-1\n");
        f.write("line-2\n");
    }
    CHECK_EQ(read_file(dir / "app.log"), "line-2\n");
    CHECK_EQ(read_file(dir / "app.1.log"), "line-1\n");
}

// A reader holding the live file open makes the rename fail on Windows. The
// live file must keep its content, not be truncated.
static void failed_rotation_keeps_live_content()
{
    auto dir = fresh_dir("busy-base");
    auto f = rotating_file{dir / "app.log", {.max_size = 10, .max_count = 3}};
    f.write("IMPORTANT-1\n");
    f.flush();
    {
        auto reader = std::ifstream{dir / "app.log"};
        f.write("line-2\n");
        f.flush();
    }
    f.write("line-3\n");
    f.flush();
    auto const s = all_content(dir);
    CHECK(s.find("IMPORTANT-1") != std::string::npos);
    CHECK(s.find("line-2") != std::string::npos);
    CHECK(s.find("line-3") != std::string::npos);
}

// A reader holding a rotated file open makes remove() fail on Windows. That
// must not throw out of write().
static void locked_rotation_target_does_not_throw()
{
    auto dir = fresh_dir("busy-target");
    auto f = rotating_file{dir / "app.log", {.max_size = 10, .max_count = 2}};
    f.write("line-1\n");
    f.write("line-2\n"); // app.1.log now exists
    f.flush();
    auto threw = false;
    {
        auto reader = std::ifstream{dir / "app.1.log"};
        try {
            f.write("line-3\n");
            f.write("line-4\n");
            f.flush();
        }
        catch (...) {
            threw = true;
        }
    }
    CHECK(!threw);
    CHECK(all_content(dir).find("line-4") != std::string::npos);
}

static void open_failure_recovers()
{
    auto dir = fresh_dir("recover");
    auto const blocker = dir / "sub";
    std::ofstream{blocker} << "a file where the directory should be";

    auto f = rotating_file{blocker / "app.log", {.max_size = 1000, .max_count = 2}};
    f.write("lost\n"); // cannot open: dropped
    fs::remove(blocker);
    std::this_thread::sleep_for(std::chrono::milliseconds{1100});
    f.write("kept\n");
    f.flush();
    CHECK_EQ(read_file(blocker / "app.log"), "kept\n");
}

static void max_count_one_caps_size()
{
    auto dir = fresh_dir("max1");
    {
        auto f = rotating_file{dir / "app.log", {.max_size = 20, .max_count = 1}};
        for (int i = 0; i < 100; ++i)
            f.write("line-" + std::to_string(i % 10) + "\n");
    }
    CHECK(fs::file_size(dir / "app.log") <= 20);
    CHECK(!fs::exists(dir / "app.1.log"));
}

int main()
{
    bare_filename();
    dot_in_directory_name();
    extension_is_kept_after_number();
    failed_rotation_keeps_live_content();
    locked_rotation_target_does_not_throw();
    open_failure_recovers();
    max_count_one_caps_size();
    return TEST_MAIN_RESULT();
}
