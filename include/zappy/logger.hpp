// Copyright (c) 2024-2026 Alex Denisov
#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <zappy/details/common.hpp>
#include <zappy/details/core.hpp>

namespace zappy {

namespace details {

// logger_state is a logger's name and bound attributes. It is immutable and
// shared by copies of the logger, so copying a logger is cheap and a log
// call never copies it.
struct logger_state {
    std::string name;
    std::vector<std::string> storage; // owned text the attributes point at
    std::vector<attribute> bound;

    // derive returns a new state with more attributes bound after these.
    auto derive(attributes extra) const -> std::shared_ptr<logger_state const>
    {
        auto st = std::make_shared<logger_state>();
        st->name = name;
        // storage must not reallocate once attributes point into it
        st->storage.reserve(2 * (bound.size() + extra.size()));
        auto own = [&](std::string_view s) -> std::string_view {
            return st->storage.emplace_back(s);
        };
        auto add = [&](attribute const& a) {
            auto v = a.value;
            if (v.k == value::kind::string)
                v = value{own(v.str)};
            else if (v.k == value::kind::user) {
                auto text = std::string{};
                v.user.fn(v.user.obj, text);
                v = value{own(text)};
            }
            st->bound.push_back({own(a.key), v});
        };
        for (auto const& a : bound)
            add(a);
        for (auto const& a : extra)
            add(a);
        return st;
    }
};

using logger_state_ptr = std::shared_ptr<logger_state const>;

} // namespace details

struct weak_logger;

// logger is the logging front end: a name, bound attributes and a level
// mask, writing to a core. Copies are cheap and safe to use from any thread.
struct logger {
    logger(std::string_view name, std::shared_ptr<zappy::core> c,
        level_mask m = all_levels)
        : logger{make_state(name), std::move(c), m}
    {
    }

    // with_attributes returns a copy that adds these attributes to every
    // message, before the call's own. The copy keeps the level mask.
    auto with_attributes(attributes aa) const -> logger
    {
        return logger{st_->derive(aa), core_, levels_};
    }

    // weak returns a logger that does not keep the core alive: for use
    // inside sinks, to avoid a reference cycle (sink -> logger -> core).
    auto weak() const -> weak_logger;

    auto should_log(level l) const -> bool
    {
        return levels_.contains(l) && core_ && core_->accepts(l);
    }

    void log(level l, std::string_view msg, attributes aa = {}) const noexcept
    {
        if (should_log(l))
            submit(*st_, *core_, l, msg, aa);
    }

    void debug(std::string_view m, attributes aa = {}) const noexcept
    {
        log(level::debug, m, aa);
    }
    void info(std::string_view m, attributes aa = {}) const noexcept
    {
        log(level::info, m, aa);
    }
    void warn(std::string_view m, attributes aa = {}) const noexcept
    {
        log(level::warn, m, aa);
    }
    void error(std::string_view m, attributes aa = {}) const noexcept
    {
        log(level::error, m, aa);
    }
    void critical(std::string_view m, attributes aa = {}) const noexcept
    {
        log(level::critical, m, aa);
    }

    auto core() const -> std::shared_ptr<zappy::core> const& { return core_; }

private:
    friend struct weak_logger;

    details::logger_state_ptr st_;
    std::shared_ptr<zappy::core> core_;
    level_mask levels_;

    logger(details::logger_state_ptr st, std::shared_ptr<zappy::core> c,
        level_mask m)
        : st_{std::move(st)}
        , core_{std::move(c)}
        , levels_{m}
    {
    }

    static auto make_state(std::string_view name) -> details::logger_state_ptr
    {
        auto st = std::make_shared<details::logger_state>();
        st->name = name;
        return st;
    }

    static void submit(details::logger_state const& st, zappy::core& c,
        level l, std::string_view msg, attributes aa) noexcept
    {
        try {
            c.submit(record{
                .time = clock::now(),
                .lvl = l,
                .logger = st.name,
                .message = msg,
                .bound = st.bound,
                .attrs = {aa.begin(), aa.size()},
            });
        }
        catch (...) {
            // out of memory while formatting; nothing better to do
        }
    }
};

// weak_logger logs like a logger while its core is alive and does nothing
// after. Use it inside sinks to log into their own core without a cycle.
struct weak_logger {
    explicit weak_logger(logger const& l)
        : st_{l.st_}
        , core_{l.core_}
        , levels_{l.levels_}
    {
    }

    void log(level l, std::string_view msg, attributes aa = {}) const noexcept
    {
        if (!levels_.contains(l))
            return;
        if (auto c = core_.lock(); c && c->accepts(l))
            logger::submit(*st_, *c, l, msg, aa);
    }

    void debug(std::string_view m, attributes aa = {}) const noexcept
    {
        log(level::debug, m, aa);
    }
    void info(std::string_view m, attributes aa = {}) const noexcept
    {
        log(level::info, m, aa);
    }
    void warn(std::string_view m, attributes aa = {}) const noexcept
    {
        log(level::warn, m, aa);
    }
    void error(std::string_view m, attributes aa = {}) const noexcept
    {
        log(level::error, m, aa);
    }
    void critical(std::string_view m, attributes aa = {}) const noexcept
    {
        log(level::critical, m, aa);
    }

private:
    details::logger_state_ptr st_;
    std::weak_ptr<zappy::core> core_;
    level_mask levels_;
};

inline auto logger::weak() const -> weak_logger { return weak_logger{*this}; }

} // namespace zappy
