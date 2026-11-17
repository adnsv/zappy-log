// Copyright (c) 2024-2026 Alex Denisov

// zappy first, then windows.h: zappy's Win32 declarations must match the SDK.

#include <zappy/sinks/console.hpp>

#include <windows.h>

auto zappy_first() -> int
{
    DWORD mode = 0;
    GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &mode);
    return GetModuleHandleW(L"kernel32.dll") != nullptr ? 1 : 0;
}
