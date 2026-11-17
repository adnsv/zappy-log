// Copyright (c) 2024-2026 Alex Denisov
#pragma once

#include <array>
#include <cstddef>
#include <string>
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

// json_special[b] is true for bytes that cannot be copied as they are: control
// characters, '"', '\\', and every byte >= 0x80 (checked as UTF-8).
inline constexpr auto json_special = [] {
    auto t = std::array<bool, 256>{};
    for (int i = 0; i < 0x20; ++i)
        t[i] = true;
    t['"'] = t['\\'] = true;
    for (int i = 0x80; i < 0x100; ++i)
        t[i] = true;
    return t;
}();

// json_scramble appends s to out, escaped for use inside a JSON string.
// Invalid UTF-8 bytes are replaced with �, so the output is always valid
// JSON. Runs of ordinary bytes are copied in one go.
inline void json_scramble(std::string& out, std::string_view s)
{
    auto p = reinterpret_cast<unsigned char const*>(s.data());
    auto const end = p + s.size();
    auto run = p; // start of the pending run of bytes to copy as they are

    auto flush_run = [&] {
        out.append(reinterpret_cast<char const*>(run), std::size_t(p - run));
    };

    while (p != end) {
        if (!json_special[*p]) {
            ++p;
            continue;
        }
        if (*p >= 0x80) {
            if (auto n = utf8_seq_len(p, end)) {
                p += n; // valid UTF-8 stays in the run
                continue;
            }
            flush_run();
            out += "\\ufffd";
            run = ++p;
            continue;
        }
        flush_run();
        switch (*p) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\b':
            out += "\\b";
            break;
        case '\f':
            out += "\\f";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default: {
            static constexpr char hex[] = "0123456789abcdef";
            char buf[] = "\\u00XX";
            buf[4] = hex[*p >> 4];
            buf[5] = hex[*p & 0xf];
            out.append(buf, 6);
        }
        }
        run = ++p;
    }
    flush_run();
}

} // namespace zappy::details
