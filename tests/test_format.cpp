#include "check.hpp"

#include <zappy/details/fmt.hpp>

using namespace std::chrono;

static auto ts(zappy::clock::time_point t) -> std::string
{
    return zappy::to_string(t);
}

static void timestamps()
{
    CHECK_EQ(ts(zappy::clock::time_point{}), "1970-01-01T00:00:00.000Z");
    CHECK_EQ(ts(zappy::clock::time_point{seconds{1763391617} + microseconds{694321}}),
        "2025-11-17T15:00:17.694Z");
    CHECK_EQ(ts(sys_days{2000y / 2 / 29} + 12h + 34min + 56s + 789ms),
        "2000-02-29T12:34:56.789Z");
    CHECK_EQ(ts(sys_days{2100y / 3 / 1} + 23h + 59min + 59s + 999ms),
        "2100-03-01T23:59:59.999Z");
    // before the epoch: floor, not truncate toward zero
    CHECK_EQ(ts(zappy::clock::time_point{} - 500ms), "1969-12-31T23:59:59.500Z");
}

static auto json(zappy::msg const& m) -> std::string
{
    auto out = std::string{};
    zappy::to_json(out, m);
    return out;
}

static auto make_msg(std::string_view logger, std::string const& text,
    std::initializer_list<zappy::attribute> aa = {}) -> zappy::msg
{
    auto m = zappy::msg{zappy::level::info, text, aa};
    m.timestamp = zappy::clock::time_point{};
    m.logger_name = logger;
    return m;
}

static void json_shape_uses_msg_key()
{
    CHECK_EQ(json(make_msg("app", "hello", {{"k", "v"}})),
        R"({"ts":"1970-01-01T00:00:00.000Z","logger":"app","level":"info","msg":"hello","k":"v"})");
}

static void json_escapes_logger_name()
{
    CHECK_EQ(json(make_msg("svc\"x\\y", "m")),
        R"({"ts":"1970-01-01T00:00:00.000Z","logger":"svc\"x\\y","level":"info","msg":"m"})");
}

static void json_replaces_invalid_utf8()
{
    auto const valid = std::string{"caf\xc3\xa9 \xf0\x9f\x98\x80"};
    CHECK_EQ(json(make_msg("", valid)),
        R"({"ts":"1970-01-01T00:00:00.000Z","level":"info","msg":")" +
            valid + R"("})");

    // stray continuation, truncated sequence, overlong, surrogate, > U+10FFFF
    CHECK_EQ(json(make_msg("", "a\xff"
                               "b\xc3"
                               "c\xc0\xaf"
                               "d\xed\xa0\x80"
                               "e\xf4\x90\x80\x80")),
        "{\"ts\":\"1970-01-01T00:00:00.000Z\",\"level\":\"info\",\"msg\":"
        "\"a\\ufffdb\\ufffdc\\ufffd\\ufffdd\\ufffd\\ufffd\\ufffde"
        "\\ufffd\\ufffd\\ufffd\\ufffd\"}");
}

static void text_escapes_logger_name()
{
    auto out = std::string{};
    zappy::to_text(out, make_msg("a\nb", "m"));
    CHECK(out.find('\n') == std::string::npos);
    CHECK(out.find("[a\\nb]") != std::string::npos);

    auto f = zappy::ansi_fmt{false};
    f.format(out, make_msg("a\nb", "m"));
    CHECK(out.find('\n') == std::string::npos);
}

// Text output must stay unambiguous: a key, value or message that could be
// read as more attributes is quoted (logfmt style).
static auto text(zappy::msg const& m) -> std::string
{
    auto out = std::string{};
    zappy::to_text(out, m);
    return out;
}

static auto tail(std::string const& s) -> std::string
{
    // everything after "[info] " (level padding included)
    return s.substr(s.find("] ") + 2);
}

static void text_quotes_ambiguous_fields()
{
    CHECK_EQ(tail(text(make_msg("", "plain", {{"k", "v"}, {"n", "42"}}))),
        " plain | k=v | n=42");
    CHECK_EQ(tail(text(make_msg("", "m", {{"user", "bob | admin=true"}}))),
        R"( m | user="bob | admin=true")");
    CHECK_EQ(tail(text(make_msg("", "m", {{"k=v", "x"}}))), R"( m | "k=v"=x)");
    CHECK_EQ(tail(text(make_msg("", "m", {{"k", ""}}))), R"( m | k="")");
    CHECK_EQ(tail(text(make_msg("", "m", {{"k", "a\"b"}}))), R"( m | k="a\"b")");
    CHECK_EQ(tail(text(make_msg("", "m", {{"k", "two words"}}))),
        R"( m | k="two words")");
    // a message is quoted only when it could be read as attributes
    CHECK_EQ(tail(text(make_msg("", "x=5 done"))), " x=5 done");
    CHECK_EQ(tail(text(make_msg("", "a | b=c"))), R"( "a | b=c")");

    auto out = std::string{};
    auto f = zappy::ansi_fmt{false};
    f.format(out, make_msg("", "a | b=c", {{"user", "bob | admin=true"}}));
    CHECK(out.find(R"("a | b=c" | user="bob | admin=true")") != std::string::npos);
}

static void null_char_pointer_attribute()
{
    char const* missing = nullptr;
    auto a = zappy::attribute{"env", missing};
    CHECK_EQ(a.value, "");
    auto b = zappy::attribute{"k", "v"};
    CHECK_EQ(b.value, "v");
    auto s = std::string{"str"};
    auto c = zappy::attribute{s, std::string_view{s}};
    CHECK_EQ(c.key, "str");
}

static void level_names()
{
    // zap has no "critical"; its closest level name is "fatal"
    CHECK_EQ(zappy::to_string(zappy::level::critical), "fatal");
    CHECK_EQ(zappy::to_string(zappy::level::critical, true), "FATAL");

    auto m = make_msg("", "m");
    m.level = zappy::level::critical;
    CHECK(json(m).find(R"("level":"fatal")") != std::string::npos);
}

int main()
{
    timestamps();
    json_shape_uses_msg_key();
    json_escapes_logger_name();
    json_replaces_invalid_utf8();
    text_escapes_logger_name();
    text_quotes_ambiguous_fields();
    null_char_pointer_attribute();
    level_names();
    return TEST_MAIN_RESULT();
}
