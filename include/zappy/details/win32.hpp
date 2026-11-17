// Copyright (c) 2024-2026 Alex Denisov
#pragma once

// The few Win32 functions zappy needs, declared here so that public headers
// do not include <windows.h> and leak its macros (ERROR, min, max, near,
// interface, ...) into user code.
//
// Each declaration has exactly the type the Windows SDK and MinGW headers
// give it, so a translation unit may also include <windows.h>, before or
// after zappy. tests/test_win32*.cpp check both orders. HMODULE is
// HINSTANCE__* because the SDKs define STRICT by default; a program that
// defines NO_STRICT cannot include both.

#ifdef _WIN32

#include <cstddef>
#include <cstdint>

struct HINSTANCE__;

extern "C" {
__declspec(dllimport) int __stdcall GetConsoleMode(
    void* hConsoleHandle, unsigned long* lpMode);
__declspec(dllimport) int __stdcall SetConsoleMode(
    void* hConsoleHandle, unsigned long dwMode);
__declspec(dllimport) unsigned long __stdcall GetFileType(void* hFile);
__declspec(dllimport) HINSTANCE__* __stdcall GetModuleHandleW(
    wchar_t const* lpModuleName);
__declspec(dllimport) std::intptr_t(__stdcall* __stdcall GetProcAddress(
    HINSTANCE__* hModule, char const* lpProcName))();
}

namespace zappy::details::win32 {

inline void* const invalid_handle_value =
    reinterpret_cast<void*>(std::intptr_t(-1));
inline constexpr unsigned long file_type_pipe = 3;
inline constexpr unsigned long enable_virtual_terminal_processing = 0x0004;
inline constexpr int file_name_info = 2; // FILE_INFO_BY_HANDLE_CLASS
inline constexpr std::size_t max_path = 260;

// GetFileInformationByHandleEx, with its enum parameter passed as int
using get_file_information_by_handle_ex_fn = int(__stdcall*)(
    void* hFile, int FileInformationClass, void* lpFileInformation,
    unsigned long dwBufferSize);

} // namespace zappy::details::win32

#endif
