#pragma once

#include <array>
#include <cstring>
#include <iostream>

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
    const auto ptrGetFileInformationByHandleEx =
        reinterpret_cast<decltype(&GetFileInformationByHandleEx)>(
            GetProcAddress(GetModuleHandle(TEXT("kernel32.dll")),
                "GetFileInformationByHandleEx"));
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

auto is_terminal(std::streambuf const* osbuf) -> bool
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

auto is_color_terminal() -> bool
{
#ifdef _WIN32
    return true;
#else
    static const bool result = []() {
        if (!std::getenv("COLORTERM"))
            return true;

        static constexpr std::array<char const*, 16> terms = {{"ansi", "color",
            "console", "cygwin", "gnome", "konsole", "kterm", "linux", "msys",
            "putty", "rxvt", "screen", "vt100", "xterm", "alacritty", "vt102"}};

        auto env_term = std::getenv("TERM");
        if (!env_term)
            return false;

        return std::any_of(
            terms.begin(), terms.end(), [env_term](char const* term) {
                return std::strstr(env_term, term) != nullptr;
            });
    }();

    return result;
#endif
}

} // namespace zappy::details