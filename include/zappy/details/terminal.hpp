// Copyright (c) 2024-2026 Alex Denisov
#pragma once

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <new>
#include <string>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace zappy::details {
#ifdef _WIN32

// from: https://github.com/agauniyal/rang/

inline bool is_msys_pty(int fd) noexcept
{
    // Dynamic load for binary compability with old Windows
    // via void(*)() so GCC's -Wcast-function-type accepts the conversion
    const auto ptrGetFileInformationByHandleEx =
        reinterpret_cast<decltype(&GetFileInformationByHandleEx)>(
            reinterpret_cast<void (*)()>(
                GetProcAddress(GetModuleHandle(TEXT("kernel32.dll")),
                    "GetFileInformationByHandleEx")));
    if (!ptrGetFileInformationByHandleEx) {
        return false;
    }

    HANDLE h = reinterpret_cast<HANDLE>(_get_osfhandle(fd));
    if (h == INVALID_HANDLE_VALUE) {
        return false;
    }

    // Check that it's a pipe:
    if (GetFileType(h) != FILE_TYPE_PIPE) {
        return false;
    }

    // POD type is binary compatible with FILE_NAME_INFO from WinBase.h
    // It have the same alignment and used to avoid UB in caller code
    struct MY_FILE_NAME_INFO {
        DWORD FileNameLength;
        WCHAR FileName[MAX_PATH];
    };

    auto pNameInfo = std::unique_ptr<MY_FILE_NAME_INFO>(
        new (std::nothrow) MY_FILE_NAME_INFO());
    if (!pNameInfo) {
        return false;
    }

    // Check pipe name is template of
    // {"cygwin-","msys-"}XXXXXXXXXXXXXXX-ptyX-XX
    if (!ptrGetFileInformationByHandleEx(
            h, FileNameInfo, pNameInfo.get(), sizeof(MY_FILE_NAME_INFO))) {
        return false;
    }
    std::wstring name(
        pNameInfo->FileName, pNameInfo->FileNameLength / sizeof(WCHAR));
    if ((name.find(L"msys-") == std::wstring::npos &&
            name.find(L"cygwin-") == std::wstring::npos) ||
        name.find(L"-pty") == std::wstring::npos) {
        return false;
    }

    return true;
}
#endif

inline auto is_terminal(std::streambuf const* osbuf) -> bool
{
    using std::cerr;
    using std::clog;
    using std::cout;

#ifdef _WIN32
    if (osbuf == cout.rdbuf()) {
        static const bool cout_term =
            (_isatty(_fileno(stdout)) || is_msys_pty(_fileno(stdout)));
        return cout_term;
    }
    else if (osbuf == cerr.rdbuf() || osbuf == clog.rdbuf()) {
        static const bool cerr_term =
            (_isatty(_fileno(stderr)) || is_msys_pty(_fileno(stderr)));
        return cerr_term;
    }
#else
    if (osbuf == cout.rdbuf()) {
        static const bool cout_term = isatty(fileno(stdout)) != 0;
        return cout_term;
    }
    else if (osbuf == cerr.rdbuf() || osbuf == clog.rdbuf()) {
        static const bool cerr_term = isatty(fileno(stderr)) != 0;
        return cerr_term;
    }
#endif
    return false;
}

// color_from_env decides from NO_COLOR, COLORTERM and TERM (null when unset)
// whether a terminal takes ANSI colors. See https://no-color.org.
inline auto color_from_env(
    char const* no_color, char const* colorterm, char const* term) -> bool
{
    if (no_color && *no_color)
        return false;
    if (colorterm && *colorterm)
        return true;
    if (!term || std::strcmp(term, "dumb") == 0)
        return false;

    static constexpr std::array<char const*, 16> terms = {{"ansi", "color",
        "console", "cygwin", "gnome", "konsole", "kterm", "linux", "msys",
        "putty", "rxvt", "screen", "vt100", "xterm", "alacritty", "vt102"}};
    return std::any_of(terms.begin(), terms.end(),
        [term](char const* t) { return std::strstr(term, t) != nullptr; });
}

#ifdef _WIN32
// enable_vt_mode turns on ANSI escape processing for a Windows console.
// Returns false for an old console that does not support it.
inline auto enable_vt_mode(int fd) -> bool
{
    HANDLE h = reinterpret_cast<HANDLE>(_get_osfhandle(fd));
    DWORD mode = 0;
    if (h == INVALID_HANDLE_VALUE || !GetConsoleMode(h, &mode))
        return false;
    if (mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING)
        return true;
    return SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
}
#endif

// is_color_terminal reports whether colors should be written to osbuf, which
// is_terminal has already found to be a terminal.
inline auto is_color_terminal(std::streambuf const* osbuf) -> bool
{
    if (auto nc = std::getenv("NO_COLOR"); nc && *nc)
        return false;

#ifdef _WIN32
    // mintty and other msys/cygwin terminals interpret ANSI themselves; a
    // Windows console needs VT processing switched on
    auto const fd = osbuf == std::cout.rdbuf() ? _fileno(stdout)
                                               : _fileno(stderr);
    return is_msys_pty(fd) || enable_vt_mode(fd);
#else
    (void)osbuf;
    static const bool result = color_from_env(std::getenv("NO_COLOR"),
        std::getenv("COLORTERM"), std::getenv("TERM"));
    return result;
#endif
}

} // namespace zappy::details