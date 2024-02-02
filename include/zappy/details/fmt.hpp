#pragma once

#include <zappy/details/ansi.hpp>
#include <zappy/details/common.hpp>
#include <zappy/details/json-scrambler.hpp>
#include <zappy/details/stringers.hpp>

namespace zappy {

// formatting as json
inline void to_json(std::string& out, msg const& m)
{
    out.clear();
    auto w = [&](std::string_view sv) { out += sv; };

    w("{\"timestamp\":\"");
    char buf[27];
    auto [p, _] = to_chars(buf, buf + 27, m.timestamp);
    w(std::string_view(buf, p - buf));
    w("\"");

    if (!m.logger_name.empty()) {
        w(",\"logger\":\"");
        w(m.logger_name);
        w("\"");
    }

    w(",\"level\":\"");
    w(zappy::to_sv(m.level));
    w("\"");

    w(",\"message\":\"");
    details::json_scramble(w, m.message);
    w("\"");

    for (auto const& attr : m.attributes) {
        w(",\"");
        details::json_scramble(w, attr.key);
        w("\":\"");
        details::json_scramble(w, attr.value);
        w("\"");
    }

    w("}");
}

// formatting as text
inline void to_text(std::string& out, msg const& m)
{
    out.clear();
    auto w = [&](std::string_view sv) { out += sv; };

    char buf[27];
    auto [p, _] = to_chars(buf, buf + 27, m.timestamp);
    w(std::string_view(buf, p - buf));

    if (!m.logger_name.empty()) {
        w(" [");
        w(m.logger_name);
        w("]");
    }

    w(" [");
    auto const level_str = zappy::to_sv(m.level);
    w(level_str);
    w("]");

    if (auto n = level_str.size(); n < 5)
        w(std::string_view("     ").substr(0, 5 - n));

    w(" ");
    details::json_scramble(w, m.message);

    for (auto const& attr : m.attributes) {
        w(" | ");
        details::json_scramble(w, attr.key);
        w("=");
        details::json_scramble(w, attr.value);
    }
}

struct ansi_fmt {
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

    ansi_fmt(bool use_ansi_sequences);
    void format(std::string&, msg const&);
};

inline ansi_fmt::ansi_fmt(bool use_ansi_sequences)
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
    };
}

inline void ansi_fmt::format(std::string& out, msg const& m)
{
    out.clear();
    auto w = [&](std::string_view sv) { out += sv; };

    auto wsection = [&](section const& fmt, std::string_view v) {
        w(fmt.before);
        w(v);
        w(fmt.after);
    };

    char buf[27];
    auto [p, _] = to_chars(buf, buf + 27, m.timestamp);
    wsection(timestamp, std::string_view(buf, p - buf));

    if (!m.logger_name.empty()) {
        wsection(logger, m.logger_name);
    }

    w(level.before);
    auto const level_content = zappy::to_sv(m.level);
    switch (m.level) {
    case level::debug:
        wsection(levels.debug, level_content);
        break;
    case level::info:
        wsection(levels.info, level_content);
        break;
    case level::warn:
        wsection(levels.warn, level_content);
        break;
    case level::critical:
        wsection(levels.critical, level_content);
        break;
    default:
        wsection(levels.error, level_content);
    }
    w(level.after);

    w(message.before);
    details::json_scramble(w, m.message);
    w(message.after);

    for (auto const& attribute : m.attributes) {
        w(attr.before);
        details::json_scramble(w, attribute.key);
        w(attr.between);
        details::json_scramble(w, attribute.value);
        w(attr.after);
    }
}

} // namespace zappy