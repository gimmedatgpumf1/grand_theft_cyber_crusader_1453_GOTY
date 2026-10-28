#include "client/diagnostics.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <execinfo.h>
#include <csignal>
#include <unistd.h>
#endif

namespace gtc::native {
namespace {

std::FILE* logFile = nullptr;
char logName[1024] = "gtc-run.log";

// Next to the executable, so the file is found even when the working directory
// is somewhere else entirely.
void chooseLogPath(const char* program) {
#if defined(_WIN32)
    char module[MAX_PATH] = {0};
    if (GetModuleFileNameA(nullptr, module, MAX_PATH) > 0) {
        char* slash = std::strrchr(module, '\\');
        if (slash) {
            *(slash + 1) = '\0';
            std::snprintf(logName, sizeof(logName), "%sgtc-run.log", module);
            return;
        }
    }
#else
    char resolved[1024] = {0};
    const ssize_t length = readlink("/proc/self/exe", resolved, sizeof(resolved) - 1);
    if (length > 0) {
        resolved[length] = '\0';
        char* slash = std::strrchr(resolved, '/');
        if (slash) {
            *(slash + 1) = '\0';
            std::snprintf(logName, sizeof(logName), "%sgtc-run.log", resolved);
            return;
        }
    }
#endif
    if (program && *program) std::snprintf(logName, sizeof(logName), "%s.log", program);
}

void openLog() {
    if (logFile) return;
    logFile = std::fopen(logName, "w");
    if (!logFile) {
        // A read-only directory must not stop the client from starting.
        std::snprintf(logName, sizeof(logName), "gtc-run.log");
        logFile = std::fopen(logName, "w");
    }
}

void writeLine(const char* text) {
    if (!logFile) return;
    std::fputs(text, logFile);
    std::fputc('\n', logFile);
    std::fflush(logFile);
}

#if defined(_WIN32)

const char* describe(unsigned long code) {
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION: return "access violation";
        case EXCEPTION_ILLEGAL_INSTRUCTION: return "illegal instruction";
        case EXCEPTION_INT_DIVIDE_BY_ZERO: return "integer divide by zero";
        case EXCEPTION_STACK_OVERFLOW: return "stack overflow";
        case EXCEPTION_IN_PAGE_ERROR: return "in-page error";
        case EXCEPTION_PRIV_INSTRUCTION: return "privileged instruction";
        case 0xC0000409: return "stack buffer overrun / fast fail";
        default: return "exception";
    }
}

LONG WINAPI onFault(EXCEPTION_POINTERS* info) {
    const EXCEPTION_RECORD* record = info ? info->ExceptionRecord : nullptr;
    char line[1024];
    if (!record) {
        writeLine("!! fault with no record");
        return EXCEPTION_EXECUTE_HANDLER;
    }
    const unsigned long code = static_cast<unsigned long>(record->ExceptionCode);
    std::snprintf(line, sizeof(line), "!! %s 0x%08lX at %p", describe(code), code,
                  record->ExceptionAddress);
    writeLine(line);
    if (code == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2) {
        const ULONG_PTR operation = record->ExceptionInformation[0];
        std::snprintf(line, sizeof(line), "!! %s address %p",
                      operation == 0 ? "reading" : operation == 1 ? "writing" : "executing",
                      reinterpret_cast<void*>(record->ExceptionInformation[1]));
        writeLine(line);
    }
    // Addresses with their module and offset: a bare address is useless once
    // the image is rebuilt, a module+offset survives.
    void* frames[32] = {0};
    const USHORT count = CaptureStackBackTrace(0, 32, frames, nullptr);
    for (USHORT index = 0; index < count; ++index) {
        HMODULE module = nullptr;
        char name[MAX_PATH] = {0};
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                   GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCSTR>(frames[index]), &module) &&
            module) {
            GetModuleFileNameA(module, name, MAX_PATH);
        }
        std::snprintf(line, sizeof(line), "   [%02u] %p  %s +0x%llX", index, frames[index],
                      name[0] ? name : "?", name[0]
                          ? static_cast<unsigned long long>(
                                reinterpret_cast<char*>(frames[index]) - reinterpret_cast<char*>(module))
                          : 0ull);
        writeLine(line);
    }
    writeLine("!! end of report");
    return EXCEPTION_EXECUTE_HANDLER;
}

void install() {
    // The filter is the whole job: it runs before Windows terminates the
    // process, so the record reaches the log whichever instruction faulted.
    SetUnhandledExceptionFilter(&onFault);
}

#else

void onSignal(int number) {
    char line[128];
    std::snprintf(line, sizeof(line), "!! signal %d (%s)", number, strsignal(number));
    writeLine(line);
    void* frames[32] = {0};
    const int count = backtrace(frames, 32);
    if (logFile) backtrace_symbols_fd(frames, count, fileno(logFile));
    std::fflush(logFile);
    signal(number, SIG_DFL);
    raise(number);
}

void install() {
    struct sigaction action {};
    action.sa_handler = &onSignal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESETHAND;
    sigaction(SIGSEGV, &action, nullptr);
    sigaction(SIGBUS, &action, nullptr);
    sigaction(SIGFPE, &action, nullptr);
    sigaction(SIGILL, &action, nullptr);
    sigaction(SIGABRT, &action, nullptr);
}

#endif

} // namespace

void trace(const char* note) {
    if (!logFile) openLog();
    writeLine(note);
}

void tracef(const char* format, ...) {
    char line[1024];
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(line, sizeof(line), format, arguments);
    va_end(arguments);
    trace(line);
}

void installCrashHandler(const char* program) {
    chooseLogPath(program);
    openLog();
    install();
}

const char* logPath() { return logName; }

std::string executableDirectory() {
    char buffer[1024] = {0};
#if defined(_WIN32)
    const DWORD length = GetModuleFileNameA(nullptr, buffer, sizeof(buffer));
    if (length == 0 || length >= sizeof(buffer)) return {};
    char* slash = std::strrchr(buffer, '\\');
#else
    const ssize_t length = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (length <= 0) return {};
    buffer[length] = '\0';
    char* slash = std::strrchr(buffer, '/');
#endif
    if (!slash) return {};
    *slash = '\0';
    return std::string(buffer);
}

} // namespace gtc::native
