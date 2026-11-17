#include "check.hpp"

#include <zappy/details/queue.hpp>

// Instantiate every member, so a typo in a member nobody calls still fails
// the build on every compiler.
template struct zappy::details::queue<int>;
template struct zappy::circular<int>;

using zappy::details::queue;

static void capacity_is_usable_in_full()
{
    for (std::size_t cap : {1u, 2u, 5u}) {
        auto q = queue<int>{cap};
        for (std::size_t i = 0; i < cap; ++i)
            q.push(int(i)); // must not block
        CHECK(q.full());
        CHECK_EQ(q.size(), cap);
        int v = -1;
        for (std::size_t i = 0; i < cap; ++i) {
            CHECK(q.try_pop(v));
            CHECK_EQ(v, int(i));
        }
        CHECK(q.empty());
        CHECK(!q.try_pop(v));
    }
}

static void zero_capacity_still_delivers()
{
    auto q = queue<int>{0};
    q.push(42);
    int v = 0;
    CHECK(q.try_pop(v));
    CHECK_EQ(v, 42);
}

static void wraps_around()
{
    auto q = queue<int>{3};
    int v = 0;
    for (int i = 0; i < 10; ++i) {
        q.push(i);
        q.push(i + 100);
        CHECK(q.try_pop(v));
        CHECK_EQ(v, i);
        CHECK(q.try_pop(v));
        CHECK_EQ(v, i + 100);
    }
    CHECK(q.empty());
}

static void try_push_does_not_block()
{
    auto q = queue<int>{2};
    CHECK(q.try_push(1));
    CHECK(q.try_push(2));
    CHECK(!q.try_push(3));
    int v = 0;
    CHECK(q.try_pop(v, std::chrono::milliseconds{1}));
    CHECK_EQ(v, 1);
}

static void circular_overwrites_oldest_when_full()
{
    int buf[3];
    auto c = zappy::circular<int>{buf};
    for (int i = 1; i <= 4; ++i)
        c.push_back(i);
    CHECK(c.full());
    CHECK_EQ(c.size(), std::size_t{3});
    CHECK_EQ(c.front(), 2);
}

int main()
{
    capacity_is_usable_in_full();
    zero_capacity_still_delivers();
    wraps_around();
    try_push_does_not_block();
    circular_overwrites_oldest_when_full();
    return TEST_MAIN_RESULT();
}
