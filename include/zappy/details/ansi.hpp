#pragma once

#include <iostream>
#include <string>
#include <string_view>

namespace zappy::ansi {

std::string_view const reset = "\x1b[0m";
std::string_view const bold = "\x1b[1m";
std::string_view const dark = "\x1b[2m";
std::string_view const italic = "\x1b[3m";
std::string_view const underline = "\x1b[4m";
std::string_view const blink = "\x1b[5m";
std::string_view const rapid_blink = "\x1b[5m";
std::string_view const reverse = "\x1b[7m";
std::string_view const concealed = "\x1b[8m";
std::string_view const strikethrough = "\x1b[9m";

std::string_view const black = "\x1b[30m";
std::string_view const red = "\x1b[31m";
std::string_view const green = "\x1b[32m";
std::string_view const yellow = "\x1b[33m";
std::string_view const blue = "\x1b[34m";
std::string_view const magenta = "\x1b[35m";
std::string_view const cyan = "\x1b[36m";
std::string_view const white = "\x1b[37m";

std::string_view const default_fg = "\x1b[39m";

std::string_view const on_black = "\x1b[40m";
std::string_view const on_red = "\x1b[41m";
std::string_view const on_green = "\x1b[42m";
std::string_view const on_yellow = "\x1b[43m";
std::string_view const on_blue = "\x1b[44m";
std::string_view const on_magenta = "\x1b[45m";
std::string_view const on_cyan = "\x1b[46m";
std::string_view const on_white = "\x1b[47m";

std::string_view const default_bg = "\x1b[49m";

namespace internal {
constexpr auto gray_idx(std::uint8_t level) -> uint8_t
{
    auto v = (level * 26) >> 8;
    if (v == 0)
        return 0;
    if (v == 25)
        return 15;
    return v + 231;
}
} // namespace internal

inline auto gray_fg(std::uint8_t level) -> std::string
{
    auto s = std::string{"\x1b[38;5;"};
    s += std::to_string(internal::gray_idx(level));
    s += "m";
    return s;
}

inline auto gray_bg(std::uint8_t level) -> std::string
{
    auto s = std::string{"\x1b[48;5;"};
    s += std::to_string(internal::gray_idx(level));
    s += "m";
    return s;
}

} // namespace zappy::ansi