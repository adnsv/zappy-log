// Copyright (c) 2024-2026 Alex Denisov
#pragma once

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <vector>
#include <zappy/details/circular.hpp>

namespace zappy::details {

template <typename T> struct queue {
private:
    mutable std::mutex mux_;
    std::vector<T> buffer_;
    circular<T> circular_;
    std::condition_variable not_empty;
    std::condition_variable not_full;

public:
    // capacity 0 is treated as 1, so a push always has somewhere to go
    queue(std::size_t capacity)
        : buffer_(std::max<std::size_t>(capacity, 1))
        , circular_{buffer_}
    {
    }

    void push(T&& v)
    {
        auto lock = std::unique_lock(mux_);
        not_full.wait(lock, [this] { return !this->circular_.full(); });
        circular_.push_back(std::move(v));
        not_empty.notify_one();
    }

    void push(T const& v)
    {
        auto lock = std::unique_lock(mux_);
        not_full.wait(lock, [this] { return !this->circular_.full(); });
        circular_.push_back(v);
        not_empty.notify_one();
    }

    // try_push never blocks: it returns false when the queue is full
    auto try_push(T&& v) -> bool
    {
        auto lock = std::unique_lock(mux_);
        if (circular_.full())
            return false;
        circular_.push_back(std::move(v));
        not_empty.notify_one();
        return true;
    }

    auto try_pop(T& v) -> bool
    {
        {
            auto lock = std::unique_lock(mux_);
            if (circular_.empty())
                return false;
            v = std::move(circular_.front());
            circular_.pop_front();
        }
        not_full.notify_one();
        return true;
    }

    auto try_pop(T& v, std::chrono::milliseconds wait_duration) -> bool
    {
        {
            auto lock = std::unique_lock(mux_);
            if (!not_empty.wait_for(lock, wait_duration,
                    [this] { return !this->circular_.empty(); }))
                return false;

            v = std::move(circular_.front());
            circular_.pop_front();
        }
        not_full.notify_one();
        return true;
    }

    auto size() const
    {
        auto lock = std::unique_lock(mux_);
        return circular_.size();
    }
    auto empty() const
    {
        auto lock = std::unique_lock(mux_);
        return circular_.empty();
    }
    auto full() const
    {
        auto lock = std::unique_lock(mux_);
        return circular_.full();
    }
};

} // namespace zappy::details
