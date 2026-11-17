// Copyright (c) 2024-2026 Alex Denisov

// Logging from a static destructor, through an async core held in a static,
// must neither hang nor lose messages: before shutdown the worker writes
// them, after shutdown the caller does.

#include "check.hpp"

#include <atomic>
#include <cstdlib>
#include <zappy/logger.hpp>
#include <zappy/sinks/func.hpp>

static std::atomic<int> delivered{0};

struct logs_at_exit {
    std::shared_ptr<zappy::core> core;
    ~logs_at_exit()
    {
        {
            auto lg = zappy::logger{"exit", core};
            for (int i = 0; i < 50; ++i)
                lg.info("bye");
            core->shutdown(); // drains the 50
            for (int i = 0; i < 50; ++i)
                lg.info("after shutdown"); // written synchronously
        }
        core.reset();
        if (delivered != 100) {
            std::fprintf(stderr, "delivered %d of 100\n", delivered.load());
            std::_Exit(1);
        }
    }
};

static logs_at_exit global;

int main()
{
    global.core = zappy::make_async_core(
        {zappy::func_sink([](std::string_view) { ++delivered; })},
        {.capacity = 4});
    return 0;
}
