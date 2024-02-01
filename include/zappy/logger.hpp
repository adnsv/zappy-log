#pragma once

#include <memory>
#include <string_view>
#include <vector>

#include <zappy/details/common.hpp>
#include <zappy/details/core.hpp>

namespace zappy {

struct logger {
private:
    std::string name_;
    std::shared_ptr<core> core_;
    std::vector<attribute> attributes_;

public:
    level_filter levels;

    using attr_init_list = std::initializer_list<attribute>;

    logger(std::string_view name, std::shared_ptr<core> c);
    logger(logger const&) = default;
    logger(logger&&) = default;

    auto with_attributes(attr_init_list attrs) -> logger;

    auto should_log(level v) const -> bool;

    void log(msg&& m) const;

    void log(
        zappy::level l, std::string const& m, attr_init_list aa = {}) const;

    void debug(std::string const& m, attr_init_list aa = {}) const
    {
        log(level::debug, m, aa);
    }
    void info(std::string const& m, attr_init_list aa = {}) const
    {
        log(level::debug, m, aa);
    }
    void warn(std::string const& m, attr_init_list aa = {}) const
    {
        log(level::warn, m, aa);
    }
    void error(std::string const& m, attr_init_list aa = {}) const
    {
        log(level::error, m, aa);
    }
};

inline logger::logger(std::string_view name, std::shared_ptr<core> c)
    : name_{name}
    , core_{c}
{
}

inline auto logger::with_attributes(attr_init_list attrs) -> logger
{
    auto ret = logger{*this};
    ret.attributes_.insert(ret.attributes_.end(), attrs.begin(), attrs.end());
    return ret;
}

inline auto logger::should_log(level v) const -> bool
{
    return core_ && (!levels || levels(v));
}

inline void logger::log(msg&& m) const
{
    if (!should_log(m.level))
        return;

    m.logger_name = name_;
    if (!attributes_.empty())
        m.attributes.insert(
            m.attributes.end(), attributes_.begin(), attributes_.end());
    if (core_)
        core_->write(std::move(m));
}

inline void logger::log(
    zappy::level l, std::string const& m, attr_init_list aa) const
{
    if (!should_log(l))
        return;
    log(msg{l, m, aa});
}

} // namespace zappy