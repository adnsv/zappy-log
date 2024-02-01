#pragma once

#include <charconv>
#include <functional>
#include <string>
#include <string_view>

#include <zappy/details/common.hpp>

namespace zappy {

constexpr auto to_chars(char* first, char* last, std::string_view s)
    -> std::to_chars_result
{
    if (first + s.size() > last) {
        first = last;
        return {last, std::errc::value_too_large};
    }
    for (auto& c : s)
        *first++ = c;
    return {first, std::errc{}};
}

inline auto to_sv(level v, bool upper_case = false) -> std::string_view
{
    switch (v) {
    case level::debug:
        return upper_case ? "DEBUG" : "debug";
    case level::info:
        return upper_case ? "INFO" : "info";
    case level::warn:
        return upper_case ? "WARN" : "warn";
    case level::error:
        return upper_case ? "ERROR" : "error";
    default:
        return "<INVALID>";
    }
}

inline auto to_chars(char* first, char* last, level v, bool upper_case = false)
    -> std::to_chars_result
{
    return to_chars(first, last, to_sv(v, upper_case));
}

inline auto to_string(level v, bool upper_case = false) -> std::string
{
    char buf[16];
    auto [p, _] = to_chars(buf, buf + 27, v, upper_case);
    return std::string(buf, p);
}

inline auto to_chars(char* first, char* last, clock::time_point const& t)
    -> std::to_chars_result
{
    if (last - first < 27)
        return {last, std::errc::value_too_large};

    static constexpr long long posix_epoch =
        62135683200ll; // UTC epoch,  seconds

    auto const since_epoch = t.time_since_epoch();
    auto const sec_since_epoch =
        std::chrono::duration_cast<std::chrono::seconds>(since_epoch);
    auto u = std::time_t(posix_epoch + sec_since_epoch.count());
    auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(since_epoch)
            .count() %
        1000;

    // Rata Die algorithm by Peter Baum
    auto rdn = unsigned(u / 86400);
    auto sod = unsigned(u % 86400); // second of a day

    auto z = rdn + 306;
    auto h = 100 * z - 25;
    auto a = h / 3652425;
    auto b = a - (a >> 2);
    auto y = (100 * b + h) / 36525;
    auto c = b + z - (1461 * y >> 2);
    auto m = (535 * c + 48950) >> 14;
    if (m > 12) {
        ++y;
        m -= 12;
    };
    static constexpr uint16_t _day_offset[12] = {
        306, 337, 0, 31, 61, 92, 122, 153, 184, 214, 245, 275};
    auto d = c - _day_offset[m - 1];

    first[26] = 'Z';

    first[25] = '0' + (milliseconds % 10);
    milliseconds /= 10;
    first[24] = '0' + (milliseconds % 10);
    milliseconds /= 10;
    first[23] = '0' + (milliseconds % 10);
    milliseconds /= 10;
    first[22] = '0' + (milliseconds % 10);
    milliseconds /= 10;
    first[21] = '0' + (milliseconds % 10);
    milliseconds /= 10;
    first[20] = '0' + milliseconds;

    first[19] = '.';

    first[18] = '0' + (sod % 10);
    sod /= 10;
    first[17] = '0' + (sod % 6);
    sod /= 6;
    first[16] = ':';
    first[15] = '0' + (sod % 10);
    sod /= 10;
    first[14] = '0' + (sod % 6);
    sod /= 6;
    first[13] = ':';
    first[12] = '0' + (sod % 10);
    sod /= 10;
    first[11] = '0' + (sod % 10);

    first[10] = 'T';

    first[9] = '0' + (d % 10);
    first[8] = '0' + (d / 10);
    first[7] = '-';
    first[6] = '0' + (m % 10);
    first[5] = '0' + (m / 10);
    first[4] = '-';
    first[3] = '0' + (y % 10);
    y /= 10;
    first[2] = '0' + (y % 10);
    y /= 10;
    first[1] = '0' + (y % 10);
    first[0] = '0' + (y / 10);

    return {first + 27, std::errc{}};
}

inline auto to_string(clock::time_point const& t) -> std::string
{
    char buf[27];
    auto [p, _] = to_chars(buf, buf + 27, t);
    return std::string(buf, p);
}

} // namespace zappy