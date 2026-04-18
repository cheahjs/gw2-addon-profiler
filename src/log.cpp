#include "log.h"
#include <cstdarg>
#include <cstdio>

namespace Log {

static FILE* s_file = nullptr;

static void FormatMessage(char* buf, size_t bufSize, const char* fmt, va_list args) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    int offset = snprintf(buf, bufSize, "[%02d:%02d:%02d.%03d] ",
                          st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    if (offset > 0 && (size_t)offset < bufSize)
        vsnprintf(buf + offset, bufSize - offset, fmt, args);
}

void Init() {
    // Log file next to our DLL, using narrow path for Wine compatibility
    char path[MAX_PATH];
    HMODULE hm = nullptr;
    GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&Init), &hm);
    GetModuleFileNameA(hm, path, MAX_PATH);

    // Replace .dll with .log
    char* dot = strrchr(path, '.');
    if (dot) strcpy(dot, ".log");
    else strcat(path, ".log");

    s_file = fopen(path, "w");

    // Log whether file opened, via OutputDebugString (always works under Wine)
    char msg[512];
    snprintf(msg, sizeof(msg), "[gw2-addon-profiler] Log file: %s (%s)\n",
             path, s_file ? "OK" : "FAILED");
    OutputDebugStringA(msg);
}

void Shutdown() {
    if (s_file) {
        fclose(s_file);
        s_file = nullptr;
    }
}

void Write(const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    FormatMessage(buf, sizeof(buf), fmt, args);
    va_end(args);

    // Always emit to OutputDebugString (Wine captures this)
    OutputDebugStringA(buf);
    OutputDebugStringA("\n");

    // Also write to file if available
    if (s_file) {
        fputs(buf, s_file);
        fputc('\n', s_file);
        fflush(s_file);
    }
}

} // namespace Log
