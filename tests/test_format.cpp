// Copyright (c) 2024-2026 Alex Denisov
#include "check.hpp"

#include <cmath>
#include <limits>
#include <zappy/details/fmt.hpp>

using namespace std::chrono;

static auto ts(zappy::clock::time_point t) -> std::string
{
    return zappy::to_string(t);
}

static void timestamps()
{
    CHECK_EQ(ts(zappy::clock::time_point{}), "1970-01-01T00:00:00.000Z");
    CHECK_EQ(ts(zappy::clock::time_point{
                 seconds{1763391617} + microseconds{694321}}),
        "2025-11-17T15:00:17.694Z");
    CHECK_EQ(ts(sys_days{2000y / 2 / 29} + 12h + 34min + 56s + 789ms),
        "2000-02-29T12:34:56.789Z");
    CHECK_EQ(ts(sys_days{2100y / 3 / 1} + 23h + 59min + 59s + 999ms),
        "2100-03-01T23:59:59.999Z");
    // before the epoch: floor, not truncate toward zero
    CHECK_EQ(
        ts(zappy::clock::time_point{} - 500ms), "1969-12-31T23:59:59.500Z");
}

// Formats r with f and strips the trailing newline every formatter adds.
static auto run(zappy::formatter const& f, zappy::record const& r)
    -> std::string
{
    auto out = std::string{};
    f.format(r, out);
    CHECK(!out.empty() && out.back() == '\n');
    if (!out.empty())
        out.pop_back();
    return out;
}

static auto rec(std::string_view logger, std::string_view text,
    std::initializer_list<zappy::attribute> aa = {},
    zappy::level l = zappy::level::info) -> zappy::record
{
    return zappy::record{
        .time = zappy::clock::time_point{},
        .lvl = l,
        .logger = logger,
        .message = text,
        .bound = {},
        .attrs = {aa.begin(), aa.size()},
    };
}

static auto json(zappy::record const& r) -> std::string
{
    return run(*zappy::json_format(), r);
}

static auto text(zappy::record const& r) -> std::string
{
    return run(*zappy::text_format(), r);
}

static void json_shape()
{
    CHECK_EQ(json(rec("app", "hello", {{"k", "v"}})),
        R"({"ts":"1970-01-01T00:00:00.000Z","logger":"app","level":"info","msg":"hello","k":"v"})");
    CHECK_EQ(json(rec("", "m")),
        R"({"ts":"1970-01-01T00:00:00.000Z","level":"info","msg":"m"})");
}

static void json_typed_values()
{
    auto const nan = std::numeric_limits<double>::quiet_NaN();
    auto const inf = std::numeric_limits<double>::infinity();
    CHECK_EQ(json(rec("", "m",
                 {{"i", -3}, {"u", 42u}, {"big", 18446744073709551615ull},
                     {"d", 42.5}, {"t", true}, {"f", false}})),
        R"({"ts":"1970-01-01T00:00:00.000Z","level":"info","msg":"m","i":-3,"u":42,"big":18446744073709551615,"d":42.5,"t":true,"f":false})");
    // zap writes non-finite floats as strings
    CHECK_EQ(json(rec("", "m", {{"n", nan}, {"p", inf}, {"q", -inf}})),
        R"({"ts":"1970-01-01T00:00:00.000Z","level":"info","msg":"m","n":"NaN","p":"+Inf","q":"-Inf"})");
}

static void json_bound_attributes_come_first()
{
    // a record only points at its attributes: keep both lists alive
    auto const bound = std::initializer_list<zappy::attribute>{{"b", 1}};
    auto const attrs = std::initializer_list<zappy::attribute>{{"c", 2}};
    auto r = rec("", "m");
    r.bound = {bound.begin(), bound.size()};
    r.attrs = {attrs.begin(), attrs.size()};
    CHECK_EQ(json(r),
        R"({"ts":"1970-01-01T00:00:00.000Z","level":"info","msg":"m","b":1,"c":2})");
}

static void json_escapes_logger_name()
{
    CHECK_EQ(json(rec("svc\"x\\y", "m")),
        R"({"ts":"1970-01-01T00:00:00.000Z","logger":"svc\"x\\y","level":"info","msg":"m"})");
}

static void json_replaces_invalid_utf8()
{
    auto const valid = std::string{"caf\xc3\xa9 \xf0\x9f\x98\x80"};
    CHECK_EQ(json(rec("", valid)),
        R"({"ts":"1970-01-01T00:00:00.000Z","level":"info","msg":")" + valid +
            R"("})");

    // stray continuation, truncated sequence, overlong, surrogate, > U+10FFFF
    CHECK_EQ(json(rec("", "a\xff"
                          "b\xc3"
                          "c\xc0\xaf"
                          "d\xed\xa0\x80"
                          "e\xf4\x90\x80\x80")),
        "{\"ts\":\"1970-01-01T00:00:00.000Z\",\"level\":\"info\",\"msg\":"
        "\"a\\ufffdb\\ufffdc\\ufffd\\ufffdd\\ufffd\\ufffd\\ufffde"
        "\\ufffd\\ufffd\\ufffd\\ufffd\"}");
}

static void json_escapes_control_characters()
{
    CHECK_EQ(json(rec("", std::string_view{"a\"\\\b\f\n\r\t\x01\x1f\x7f", 11})),
        R"({"ts":"1970-01-01T00:00:00.000Z","level":"info","msg":"a\"\\\b\f\n\r\t\u0001\u001f)"
        "\x7f"
        R"("})");
}

static void large_message_is_escaped_correctly()
{
    auto big = std::string(100000, 'x');
    big[50000] = '"';
    big[99999] = '\n';
    auto const out = json(rec("", big));
    CHECK(out.size() == big.size() + 2 +
                            std::string_view{R"({"ts":"1970-01-01T00:00:00.000Z","level":"info","msg":""})"}
                                .size());
    CHECK(out.find("x\\\"x") != std::string::npos);
    CHECK(out.find("x\\n\"}") != std::string::npos);
}

static void text_escapes_logger_name()
{
    auto const out = text(rec("a\nb", "m"));
    CHECK(out.find('\n') == std::string::npos);
    CHECK(out.find("[a\\nb]") != std::string::npos);
    CHECK(run(*zappy::ansi_format(false), rec("a\nb", "m")).find('\n') ==
          std::string::npos);
}

// everything after "[info] " (level padding included)
static auto tail(std::string const& s) -> std::string
{
    return s.substr(s.find("] ") + 2);
}

// Text output must stay unambiguous: a key, value or message that could be
// read as more attributes is quoted (logfmt style).
static void text_quotes_ambiguous_fields()
{
    CHECK_EQ(tail(text(rec("", "plain", {{"k", "v"}, {"n", 42}}))),
        " plain | k=v | n=42");
    CHECK_EQ(tail(text(rec("", "m", {{"user", "bob | admin=true"}}))),
        R"( m | user="bob | admin=true")");
    CHECK_EQ(tail(text(rec("", "m", {{"k=v", "x"}}))), R"( m | "k=v"=x)");
    CHECK_EQ(tail(text(rec("", "m", {{"k", ""}}))), R"( m | k="")");
    CHECK_EQ(tail(text(rec("", "m", {{"k", "a\"b"}}))), R"( m | k="a\"b")");
    CHECK_EQ(
        tail(text(rec("", "m", {{"k", "two words"}}))), R"( m | k="two words")");
    CHECK_EQ(tail(text(rec("", "m", {{"d", 2.5}, {"t", true}}))),
        " m | d=2.5 | t=true");
    // a message is quoted only when it could be read as attributes
    CHECK_EQ(tail(text(rec("", "x=5 done"))), " x=5 done");
    CHECK_EQ(tail(text(rec("", "a | b=c"))), R"( "a | b=c")");

    auto const out = run(*zappy::ansi_format(false),
        rec("", "a | b=c", {{"user", "bob | admin=true"}}));
    CHECK(out.find(R"("a | b=c" | user="bob | admin=true")") !=
          std::string::npos);
}

static void string_like_values()
{
    char const* missing = nullptr;
    auto const s = std::string{"str"};
    CHECK_EQ(json(rec("", "m",
                 {{"null", missing}, {"lit", "v"}, {"std", s},
                     {"view", std::string_view{s}}})),
        R"({"ts":"1970-01-01T00:00:00.000Z","level":"info","msg":"m","null":"","lit":"v","std":"str","view":"str"})");
}

struct point {
    int x, y;
};

template <> struct zappy::value_formatter<point> {
    void operator()(point const& p, std::string& out) const
    {
        out += '(';
        out += std::to_string(p.x);
        out += ',';
        out += std::to_string(p.y);
        out += ')';
    }
};

static void user_type_values()
{
    auto const p = point{3, -4};
    CHECK_EQ(json(rec("", "m", {{"p", p}})),
        R"x({"ts":"1970-01-01T00:00:00.000Z","level":"info","msg":"m","p":"(3,-4)"})x");
    CHECK_EQ(tail(text(rec("", "m", {{"p", p}}))), " m | p=(3,-4)");
}

static void level_names()
{
    // zap has no "critical"; its closest level name is "fatal"
    CHECK_EQ(zappy::to_string(zappy::level::critical), "fatal");
    CHECK_EQ(zappy::to_string(zappy::level::critical, true), "FATAL");
    CHECK(json(rec("", "m", {}, zappy::level::critical))
              .find(R"("level":"fatal")") != std::string::npos);
}

static void level_masks()
{
    using zappy::level;
    constexpr auto m = zappy::levels(level::info, level::error);
    static_assert(!m.contains(level::debug));
    static_assert(m.contains(level::info) && m.contains(level::error));
    static_assert(!m.contains(level::critical));
    static_assert(zappy::all_levels.contains(level::debug));
    static_assert(zappy::levels(level::warn).contains(level::critical));
    // reversed bounds are swapped, never an empty mask
    static_assert(zappy::levels(level::error, level::debug).bits ==
                  zappy::levels(level::debug, level::error).bits);
    constexpr auto set = zappy::level_mask{level::debug, level::critical};
    static_assert(set.contains(level::debug) && !set.contains(level::info));
    static_assert(set.contains(level::critical));
}

int main()
{
    timestamps();
    json_shape();
    json_typed_values();
    json_bound_attributes_come_first();
    json_escapes_logger_name();
    json_replaces_invalid_utf8();
    json_escapes_control_characters();
    large_message_is_escaped_correctly();
    text_escapes_logger_name();
    text_quotes_ambiguous_fields();
    string_like_values();
    user_type_values();
    level_names();
    level_masks();
    return TEST_MAIN_RESULT();
}
