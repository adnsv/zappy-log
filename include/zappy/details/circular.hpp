// Copyright (c) 2024-2026 Alex Denisov
#pragma once

#include <cstddef>
#include <span>

namespace zappy {

// circular is a fixed-capacity ring buffer over caller-provided storage. All
// capacity() slots are usable; push_back on a full buffer overwrites the
// oldest item.
template <typename T> struct circular {
private:
    std::size_t head_ = 0;
    std::size_t count_ = 0;
    std::span<T> v_;

public:
    circular() = default;

    circular(std::span<T> buffer)
        : v_{buffer}
    {
    }

    circular(circular const&) = default;

    auto size() const -> std::size_t { return count_; }

    auto capacity() const -> std::size_t { return v_.size(); }

    auto empty() const -> bool { return count_ == 0; }

    auto full() const -> bool { return count_ == capacity(); }

    void push_back(T&& item)
    {
        if (v_.empty())
            return;
        v_[slot()] = std::move(item);
        advance();
    }

    void push_back(T const& item)
    {
        if (v_.empty())
            return;
        v_[slot()] = item;
        advance();
    }

    auto front() const -> T const& { return v_[head_]; }
    auto front() -> T& { return v_[head_]; }

    void pop_front()
    {
        head_ = (head_ + 1) % capacity();
        --count_;
    }

private:
    // the slot the next push_back writes to
    auto slot() const -> std::size_t { return (head_ + count_) % capacity(); }

    void advance()
    {
        if (full())
            head_ = (head_ + 1) % capacity(); // overwrote the oldest
        else
            ++count_;
    }
};

} // namespace zappy
