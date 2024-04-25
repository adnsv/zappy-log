#pragma once

#include <chrono>
#include <functional>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace zappy {

enum class level {
    debug, // lowest priority messages for detailed app debugging
    info,// info is the default logging priority
    warn,// warning messages are more important than info
    error,// high priority messages. i.e., If a system is running smoothly, it should not generate any error-level messages
    critical,// highest priority, critical errors that require special attention
};

using clock = std::chrono::system_clock;

struct attribute {
    std::string key;
    std::string value;
    attribute(attribute const&) noexcept = default;
    attribute(attribute&&) noexcept = default;
    attribute(std::string_view k, std::string_view v) noexcept
        : key{k}
        , value{v}
    {
    }
    auto operator=(attribute const&) -> attribute& = default;
    auto operator=(attribute&&) -> attribute& = default;
};

struct msg {
    clock::time_point timestamp;
    std::string logger_name;
    zappy::level level = zappy::level::info;
    std::string message;
    std::vector<attribute> attributes;

    msg() {}
    msg(zappy::level l, std::string const& m)
        : timestamp{clock::now()}
        , level{l}
        , message{m}
    {
    }
    msg(msg const&) = default;
    msg(msg&&) = default;

    template <typename AttrIter>
    msg(zappy::level l, std::string const& m, AttrIter attrs_begin,
        AttrIter attrs_end)
        : timestamp{clock::now()}
        , level{l}
        , message{m}
        , attributes{attrs_begin, attrs_end}
    {
    }

    msg(zappy::level l, std::string const& m, attribute const& a)
        : timestamp{clock::now()}
        , level{l}
        , message{m}
        , attributes{&a, (&a) + 1}
    {
    }

    msg(zappy::level l, std::string const& m,
        std::initializer_list<attribute> aa)
        : timestamp{clock::now()}
        , level{l}
        , message{m}
        , attributes{aa.begin(), aa.end()}
    {
    }

    auto operator=(msg const&) -> msg& = default;
    auto operator=(msg&&) -> msg& = default;

    auto add_attr(attribute&& a) -> msg&
    {
        attributes.push_back(std::move(a));
        return *this;
    }
    auto add_attr(std::string_view key, std::string_view value) -> msg&
    {
        attributes.push_back(attribute{std::string(key), std::string(value)});
        return *this;
    }
};

using level_filter = std::function<bool(level)>;

inline auto levels(level min_v, level max_v = level::critical) -> level_filter
{
    return [min_v, max_v](level v) -> bool { 
        return v >= min_v && v <= max_v; 
    };
}

// sink interface
struct sink {
    level_filter levels;
    sink(level_filter&& f = {})
        : levels{std::move(f)}
    {
    }
    virtual ~sink() {}
    virtual void write(msg const&) = 0;
    virtual void flush() = 0;

    auto should_log(level v) const -> bool { return !levels || levels(v); }
};
using sink_ptr = std::shared_ptr<sink>;
using sinks_init_list = std::initializer_list<sink_ptr>;

} // namespace zappy