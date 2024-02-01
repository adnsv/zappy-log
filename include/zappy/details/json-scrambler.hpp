#pragma once

#include <string_view>

namespace zappy::details {

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
            else
                ++cursor;
        }
    }
    if (cursor != start) {
        put({start, size_t(cursor - start)});
    }
}

}