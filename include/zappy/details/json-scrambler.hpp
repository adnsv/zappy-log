// Copyright (c) 2024-2026 Alex Denisov
#pragma once

#include <cstddef>
#include <string_view>

namespace zappy::details {

// utf8_seq_len returns the length of the well-formed UTF-8 sequence starting
// at p (lead byte >= 0x80), or 0 if it is not well formed: stray continuation
// bytes, truncated or overlong sequences, surrogates, and code points above
// U+10FFFF are rejected.
inline auto utf8_seq_len(unsigned char const* p, unsigned char const* end)
    -> std::size_t
{
    auto const b0 = p[0];
    auto lo = 0x80, hi = 0xbf; // allowed range of the second byte
    std::size_t n = 0;
    if (b0 >= 0xc2 && b0 <= 0xdf)
        n = 2;
    else if (b0 >= 0xe0 && b0 <= 0xef) {
        n = 3;
        if (b0 == 0xe0)
            lo = 0xa0; // overlong
        else if (b0 == 0xed)
            hi = 0x9f; // surrogates
    }
    else if (b0 >= 0xf0 && b0 <= 0xf4) {
        n = 4;
        if (b0 == 0xf0)
            lo = 0x90; // overlong
        else if (b0 == 0xf4)
            hi = 0x8f; // above U+10FFFF
    }
    else
        return 0;

    if (std::size_t(end - p) < n)
        return 0;
    if (p[1] < lo || p[1] > hi)
        return 0;
    for (std::size_t i = 2; i < n; ++i)
        if (p[i] < 0x80 || p[i] > 0xbf)
            return 0;
    return n;
}

// json_scramble escapes s for use inside a JSON string. Invalid UTF-8 bytes
// are replaced with U+FFFD, so the output is always valid JSON.
template <typename Putter> void json_scramble(Putter put, std::string_view s)
{
    if (s.empty())
        return;

    auto start = s.data();
    auto end = s.data() + s.size();
    auto cursor = start;

    auto replace = [&](std::string_view with) {
        put({start, size_t(cursor - start)});
        ++cursor;
        start = cursor;
        put(with);
    };

    while (cursor != end) {
        auto cp = *cursor;

        switch (cp) {

        case '\b':
            replace("\\b");
            break;
        case '\f':
            replace("\\f");
            break;
        case '\n':
            replace("\\n");
            break;
        case '\r':
            replace("\\r");
            break;
        case '\t':
            replace("\\t");
            break;

        case '\\':
            replace("\\\\");
            break;

        case '"':
            replace("\\\"");
            break;

        default:
            if (static_cast<unsigned char>(cp) <= '\x0f') {
                char buf[] = "\\u0000";
                buf[5] += cp;
                if (cp >= '\x0a')
                    buf[5] += 'a' - ':';
                replace(buf);
            }
            else if (static_cast<unsigned char>(cp) <= '\x1f') {
                char buf[] = "\\u0010";
                buf[5] += cp - 16;
                if (cp >= '\x1a')
                    buf[5] += 'a' - ':';
                replace(buf);
            }
            else if (static_cast<unsigned char>(cp) < 0x80)
                ++cursor;
            else if (auto n = utf8_seq_len(
                         reinterpret_cast<unsigned char const*>(cursor),
                         reinterpret_cast<unsigned char const*>(end)))
                cursor += n;
            else
                replace("\\ufffd"); // invalid UTF-8 byte
        }
    }
    if (cursor != start) {
        put({start, size_t(cursor - start)});
    }
}

}