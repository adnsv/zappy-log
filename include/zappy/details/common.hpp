// Copyright (c) 2024-2026 Alex Denisov
#pragma once

#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

namespace zappy {

enum class level : std::uint8_t {
    debug,    // lowest priority messages for detailed app debugging
    info,     // info is the default logging priority
    warn,     // warning messages are more important than info
    error,    // high priority; a smoothly running system logs none
    critical, // highest priority; written as "fatal" (zap's name)
};

// level_mask is a set of levels. Masks are fixed when a logger, core or sink
// is created.
struct level_mask {
    std::uint8_t bits = 0;

    constexpr level_mask() = default;
    constexpr level_mask(std::initializer_list<level> ll)
    {
        for (auto l : ll)
            bits |= bit(l);
    }

    constexpr auto contains(level l) const -> bool { return bits & bit(l); }

    static constexpr auto bit(level l) -> std::uint8_t
    {
        return std::uint8_t(1u << unsigned(l));
    }
};

// levels returns the mask of all levels from min to max, inclusive. Reversed
// bounds are swapped: levels(error, debug) is levels(debug, error).
constexpr auto levels(level min, level max = level::critical) -> level_mask
{
    if (max < min) {
        auto const t = min;
        min = max;
        max = t;
    }
    auto m = level_mask{};
    for (auto l = unsigned(min); l <= unsigned(max); ++l)
        m.bits |= level_mask::bit(level(l));
    return m;
}

inline constexpr level_mask all_levels = levels(level::debug);

using clock = std::chrono::system_clock;

// value_formatter<T> lets a user type be an attribute value. Specialize it
// with `void operator()(T const&, std::string& out) const` that appends the
// value's text to out; it is written as a string.
template <typename T> struct value_formatter;

template <typename T>
concept user_formattable = requires(T const& v, std::string& out) {
    value_formatter<T>{}(v, out);
};

// value is a typed attribute value. It does not own anything: it refers to
// the caller's data, which stays alive for the whole log call.
struct value {
    enum class kind : std::uint8_t {
        string,
        int64,
        uint64,
        float64,
        boolean,
        user
    };

    struct user_value {
        void const* obj;
        void (*fn)(void const*, std::string&);
    };

    kind k = kind::string;
    union {
        std::string_view str;
        std::int64_t i64;
        std::uint64_t u64;
        double f64;
        bool b;
        user_value user;
    };

    value() noexcept
        : str{}
    {
    }
    value(std::string_view s) noexcept
        : str{s}
    {
    }
    value(std::string const& s) noexcept
        : str{s}
    {
    }
    // a null pointer is written as "" rather than crashing (e.g. a getenv()
    // result passed straight through)
    value(char const* s) noexcept
        : str{s ? std::string_view{s} : std::string_view{}}
    {
    }
    value(bool v) noexcept
        : k{kind::boolean}
        , b{v}
    {
    }
    // a char would otherwise convert silently to bool; pass a string instead
    value(char) = delete;
    template <std::signed_integral T>
        requires(!std::same_as<T, char>)
    value(T v) noexcept
        : k{kind::int64}
        , i64{v}
    {
    }
    template <std::unsigned_integral T>
        requires(!std::same_as<T, bool> && !std::same_as<T, char> &&
                 !std::same_as<T, char8_t> && !std::same_as<T, char16_t> &&
                 !std::same_as<T, char32_t>)
    value(T v) noexcept
        : k{kind::uint64}
        , u64{v}
    {
    }
    template <std::floating_point T>
    value(T v) noexcept
        : k{kind::float64}
        , f64{double(v)}
    {
    }
    template <user_formattable T>
        requires(!std::is_convertible_v<T const&, std::string_view>)
    value(T const& v) noexcept
        : k{kind::user}
        , user{&v, [](void const* p, std::string& out) {
                   value_formatter<T>{}(*static_cast<T const*>(p), out);
               }}
    {
    }
};

struct attribute {
    std::string_view key;
    zappy::value value;
};

using attributes = std::initializer_list<attribute>;

// record is one log message as the formatters see it. It refers to the
// caller's data and lives only for the duration of a log call.
struct record {
    clock::time_point time;
    level lvl = level::info;
    std::string_view logger;
    std::string_view message;
    std::span<attribute const> bound; // the logger's own attributes, first
    std::span<attribute const> attrs; // the call's attributes
};

// formatter turns a record into one complete output line, including the
// trailing newline. Formatters run on the logging thread before any lock,
// so format must not touch shared mutable state.
struct formatter {
    virtual ~formatter() = default;
    virtual void format(record const& r, std::string& out) const = 0;
};
using formatter_ptr = std::shared_ptr<formatter const>;

namespace details {
struct core_state;
struct sink_access;
} // namespace details

// sink receives formatted bytes. The core calls write and flush with the
// sink's own mutex held, so a sink never needs to lock, and one sink may be
// shared by several cores.
struct sink {
    sink(formatter_ptr f, level_mask m)
        : fmt_{std::move(f)}
        , levels_{m}
    {
    }
    virtual ~sink() = default;
    sink(sink const&) = delete;
    auto operator=(sink const&) -> sink& = delete;

    virtual void write(std::string_view bytes) = 0;
    virtual void flush() = 0;

    auto levels() const -> level_mask { return levels_; }
    auto format() const -> formatter const& { return *fmt_; }

private:
    friend struct details::core_state;
    friend struct details::sink_access;
    std::mutex mtx_;
    formatter_ptr fmt_;
    level_mask levels_;
};
using sink_ptr = std::shared_ptr<sink>;

} // namespace zappy
