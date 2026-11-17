// Copyright (c) 2024-2026 Alex Denisov

// The console sink must not leak windows.h macros into user code.

#include <zappy/logger.hpp>
#include <zappy/sinks/console.hpp>

#if defined(ERROR) || defined(min) || defined(max) || defined(interface) ||    \
    defined(near) || defined(far) || defined(IN) || defined(OUT) ||            \
    defined(DELETE) || defined(GetMessage) || defined(CreateFile) ||           \
    defined(_WINDOWS_)
#error "zappy leaked windows.h macros"
#endif

// names that windows.h turns into macros must be usable
enum class status { OK, ERROR };
static int near = 1;

auto zappy_first() -> int; // test_win32_order.cpp
auto windows_first() -> int; // test_win32_order2.cpp

int main()
{
    auto ok = zappy_first() == 1 && windows_first() == 1 &&
              status::ERROR != status::OK && near == 1;
    return ok ? 0 : 1;
}
