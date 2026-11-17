// Copyright (c) 2024-2026 Alex Denisov

// windows.h first, then zappy: zappy's Win32 declarations must match the SDK.

#include <windows.h>

#include <zappy/sinks/console.hpp>

auto windows_first() -> int
{
    return zappy::details::is_terminal(nullptr) ? 0 : 1;
}
