#pragma once

#include <span>

namespace zappy {

template <typename T> struct circular {
private:
    std::size_t head_ = 0;
    std::size_t tail_ = 0;
    std::span<T> v_;

public:
    circular() = default;

    circular(std::span<T> buffer)
        : v_{buffer}
    {
    }

    circular(circular const&) = default;

    auto size() const -> std::size_t
    {
        if (tail_ >= head_) {
            return tail_ - head_;
        }
        else {
            return capacity() - (head_ - tail_);
        }
    }

    auto capacity() const -> std::size_t { return v_.size(); }

    auto empty() const -> bool { return tail_ == head_; }

    auto full() const -> bool
    {
        auto const cap = capacity();
        if (cap > 0)
            return ((tail_ + 1) % cap) == head_;

        return false;
    }

    void push_back(T&& item)
    {
        if (v_.empty())
            return;

        v_[tail_] = std::move(item);
        tail_ = (tail_ + 1) % capacity();

        if (tail_ == head_)
            head_ = (head_ + 1) % capacity();
    }

    void push_back(T const& item)
    {
        if (v_.empty())
            return;

        v_[tail_] = item;
        tail_ = (tail_ + 1) % capacity();

        if (tail_ == head_)
            head_ = (head_ + 1) % capacity();
    }

    auto front() const -> T const& { return v_[head_]; }
    auto front() -> T& { return v_[head_]; }

    void pop_front() { head_ = (head_ + 1) % capacity(); }
};

} // namespace zappy