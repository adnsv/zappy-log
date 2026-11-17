// Logging from a static destructor, after the shared worker thread may have
// stopped, must neither hang nor lose messages.

#include "check.hpp"

#include <zappy/logger.hpp>
#include <zappy/sinks/func.hpp>

static int delivered = 0;

struct logs_at_exit {
    std::shared_ptr<zappy::core> core;
    ~logs_at_exit()
    {
        {
            auto lg = zappy::logger{"exit", core};
            for (int i = 0; i < 50; ++i)
                lg.info("bye");
        }
        core.reset(); // last reference: drains the queue
        if (delivered != 50) {
            std::fprintf(stderr, "delivered %d of 50\n", delivered);
            std::_Exit(1);
        }
    }
};

static logs_at_exit global; // constructed before the worker, so destroyed after

int main()
{
    global.core = zappy::make_core(
        4, {zappy::func_sink([](zappy::msg const&) { ++delivered; }, {})});
    return 0;
}
