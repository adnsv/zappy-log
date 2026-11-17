// Copyright (c) 2024-2026 Alex Denisov
#pragma once

#include <charconv>
#include <cmath>
#include <memory>
#include <string>
#include <string_view>
#include <zappy/details/ansi.hpp>
#include <zappy/details/common.hpp>
#include <zappy/details/json-scrambler.hpp>
#include <zappy/details/stringers.hpp>

namespace zappy {

namespace details {

// user_text formats a user-type value into a per-thread scratch string.
inline auto user_text(value const& v) -> std::string const&
{
    thread_local std::string tmp;
    tmp.clear();
    v.user.fn(v.user.obj, tmp);
    return tmp;
}

// append_number writes an integer or floating point value. Non-finite
// floating point values are written as zap writes them: NaN, +Inf, -Inf.
// It returns false for a non-finite value, which JSON must quote.
inline auto append_number(std::string& out, value const& v) -> bool
{
    char buf[32];
    auto r = std::to_chars_result{};
    switch (v.k) {
    case value::kind::int64:
        r = std::to_chars(buf, buf + sizeof(buf), v.i64);
        break;
    case value::kind::uint64:
        r = std::to_chars(buf, buf + sizeof(buf), v.u64);
        break;
    default:
        if (std::isnan(v.f64)) {
            out += "NaN";
            return false;
        }
        if (std::isinf(v.f64)) {
            out += v.f64 > 0 ? "+Inf" : "-Inf";
            return false;
        }
        r = std::to_chars(buf, buf + sizeof(buf), v.f64);
    }
    out.append(buf, std::size_t(r.ptr - buf));
    return true;
}

inline void append_timestamp(std::string& out, clock::time_point t)
{
    char buf[timestamp_chars];
    auto [p, _] = to_chars(buf, buf + timestamp_chars, t);
    out.append(buf, std::size_t(p - buf));
}

inline void json_quoted(std::string& out, std::string_view s)
{
    out += '"';
    json_scramble(out, s);
    out += '"';
}

inline void json_value(std::string& out, value const& v)
{
    switch (v.k) {
    case value::kind::string:
        json_quoted(out, v.str);
        break;
    case value::kind::boolean:
        out += v.b ? "true" : "false";
        break;
    case value::kind::user:
        json_quoted(out, user_text(v));
        break;
    default: {
        auto const at = out.size();
        if (!append_number(out, v)) {
            out.insert(at, 1, '"');
            out += '"';
        }
    }
    }
}

// Characters that make a text-format key or value ambiguous: " | " separates
// attributes and "=" separates a key from its value.
inline constexpr std::string_view text_attr_specials = " |=\"";

// text_field writes s escaped, quoted (logfmt style) when it is empty or
// contains one of specials.
inline void text_field(
    std::string& out, std::string_view s, std::string_view specials)
{
    auto const quote =
        s.empty() || s.find_first_of(specials) != std::string_view::npos;
    if (quote)
        out += '"';
    json_scramble(out, s);
    if (quote)
        out += '"';
}

// text_message writes the message, quoted only if it contains "|" and so
// could be read as attributes.
inline void text_message(std::string& out, std::string_view s)
{
    if (s.find('|') == std::string_view::npos)
        json_scramble(out, s);
    else
        text_field(out, s, "|");
}

inline void text_value(std::string& out, value const& v)
{
    switch (v.k) {
    case value::kind::string:
        text_field(out, v.str, text_attr_specials);
        break;
    case value::kind::boolean:
        out += v.b ? "true" : "false";
        break;
    case value::kind::user:
        text_field(out, user_text(v), text_attr_specials);
        break;
    default:
        append_number(out, v);
    }
}

template <typename F> void for_each_attribute(record const& r, F&& f)
{
    for (auto const& a : r.bound)
        f(a);
    for (auto const& a : r.attrs)
        f(a);
}

} // namespace details

// json_formatter writes zap-compatible JSON lines:
// {"ts":"...","logger":"...","level":"...","msg":"...",<attributes>}
struct json_formatter final : formatter {
    void format(record const& r, std::string& out) const override
    {
        out.clear();
        out += "{\"ts\":\"";
        details::append_timestamp(out, r.time);
        out += '"';
        if (!r.logger.empty()) {
            out += ",\"logger\":";
            details::json_quoted(out, r.logger);
        }
        out += ",\"level\":\"";
        out += to_sv(r.lvl);
        out += "\",\"msg\":";
        details::json_quoted(out, r.message);
        details::for_each_attribute(r, [&](attribute const& a) {
            out += ',';
            details::json_quoted(out, a.key);
            out += ':';
            details::json_value(out, a.value);
        });
        out += "}\n";
    }
};

// text_formatter writes one human-readable line:
// <ts> [<logger>] [<level>] <message> | key=value | ...
struct text_formatter final : formatter {
    void format(record const& r, std::string& out) const override
    {
        out.clear();
        details::append_timestamp(out, r.time);
        if (!r.logger.empty()) {
            out += " [";
            details::json_scramble(out, r.logger);
            out += ']';
        }
        out += " [";
        auto const level_str = to_sv(r.lvl);
        out += level_str;
        out += ']';
        if (auto n = level_str.size(); n < 5)
            out.append(5 - n, ' ');
        out += ' ';
        details::text_message(out, r.message);
        details::for_each_attribute(r, [&](attribute const& a) {
            out += " | ";
            details::text_field(out, a.key, details::text_attr_specials);
            out += '=';
            details::text_value(out, a.value);
        });
        out += '\n';
    }
};

// ansi_formatter is the text format with optional ANSI colors, for consoles.
struct ansi_formatter final : formatter {
    struct section {
        std::string before;
        std::string after;
    };

    section timestamp;
    section logger;
    section level;
    struct {
        section debug;
        section info;
        section warn;
        section error;
        section critical;
    } levels;
    section message;
    struct {
        std::string before;
        std::string between;
        std::string after;
    } attr;

    explicit ansi_formatter(bool use_ansi_sequences)
    {
        auto s = [use_ansi_sequences](std::string_view v) -> std::string {
            return use_ansi_sequences ? std::string(v) : "";
        };
        timestamp = {s(ansi::dark), s(ansi::reset)};
        logger = {" [", "]"};
        level = {" ", ""};
        levels.debug = {s(ansi::blue) + '[', ']' + s(ansi::reset)};
        levels.info = {s(ansi::green) + '[', ']' + s(ansi::reset) + ' '};
        levels.warn = {s(ansi::yellow) + '[', ']' + s(ansi::reset) + ' '};
        levels.error = {s(ansi::red) + '[', ']' + s(ansi::reset)};
        levels.critical = {
            s(ansi::red) + s(ansi::bold) + '[', ']' + s(ansi::reset)};
        message = {" ", ""};
        attr = {
            .before = s(ansi::dark) + std::string(" | "),
            .between = std::string("=") + s(ansi::reset),
            .after = {},
        };
    }

    void format(record const& r, std::string& out) const override
    {
        out.clear();
        out += timestamp.before;
        details::append_timestamp(out, r.time);
        out += timestamp.after;

        if (!r.logger.empty()) {
            out += logger.before;
            details::json_scramble(out, r.logger);
            out += logger.after;
        }

        out += level.before;
        auto const& sect = [&]() -> section const& {
            switch (r.lvl) {
            case zappy::level::debug:
                return levels.debug;
            case zappy::level::info:
                return levels.info;
            case zappy::level::warn:
                return levels.warn;
            case zappy::level::critical:
                return levels.critical;
            default:
                return levels.error;
            }
        }();
        out += sect.before;
        out += to_sv(r.lvl);
        out += sect.after;
        out += level.after;

        out += message.before;
        details::text_message(out, r.message);
        out += message.after;

        details::for_each_attribute(r, [&](attribute const& a) {
            out += attr.before;
            details::text_field(out, a.key, details::text_attr_specials);
            out += attr.between;
            details::text_value(out, a.value);
            out += attr.after;
        });
        out += '\n';
    }
};

// Shared formatter instances: sinks that use the same instance share the
// formatted bytes of each message.
inline auto json_format() -> formatter_ptr
{
    static auto const f = std::make_shared<json_formatter const>();
    return f;
}

inline auto text_format() -> formatter_ptr
{
    static auto const f = std::make_shared<text_formatter const>();
    return f;
}

inline auto ansi_format(bool use_ansi_sequences) -> formatter_ptr
{
    static auto const color = std::make_shared<ansi_formatter const>(true);
    static auto const plain = std::make_shared<ansi_formatter const>(false);
    return use_ansi_sequences ? color : plain;
}

} // namespace zappy
