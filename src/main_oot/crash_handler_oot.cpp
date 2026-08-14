/*
 * Crash diagnostics for the OoT milestone launcher.
 *
 * Recompiled game code is ordinary named C functions, one per decomp symbol, so a
 * symbolized backtrace names the exact OoT function that faulted. That turns an
 * otherwise silent access violation into a one-line answer.
 *
 * This exists because ultramodern's own error paths (error_handling::message_box,
 * quick_exit, and librecomp's missing-function lookup) all print to stderr before
 * exiting. A crash that produces NO output therefore is not one of those paths --
 * it is a genuine memory fault, and nothing in the runtime reports it.
 *
 * Faults are expected to land in recompiled code reading out-of-range RDRAM: the
 * generated MEM_B/MEM_W accessors index the rdram block with no bounds check, so a
 * bad guest address dereferences a wild host pointer.
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
// uintptr_t and std::initializer_list arrive transitively via the Windows headers
// below, but not on Linux, where the backtrace path is plain libc.
#include <cstdint>
#include <initializer_list>
#include <exception>
#include <stdexcept>

#include "crash_handler_oot.hpp"

// An uncaught C++ exception does not reach a memory-fault handler: the CRT runs
// std::terminate and calls abort, which SetUnhandledExceptionFilter never sees, so
// the process dies in silence exactly like a fault would. Rethrowing the in-flight
// exception here is the only portable way to recover its type and message.
static void on_terminate() {
    fprintf(stderr, "\n============ UNCAUGHT EXCEPTION =============\n");

    if (std::exception_ptr active = std::current_exception()) {
        try {
            std::rethrow_exception(active);
        } catch (const std::exception& e) {
            fprintf(stderr, "std::exception: %s\n", e.what());
        } catch (...) {
            fprintf(stderr, "(non-std exception type)\n");
        }
    } else {
        fprintf(stderr, "(std::terminate called with no active exception)\n");
    }

    fprintf(stderr, "=============================================\n");
    fflush(stderr);

    std::abort();
}

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>

static void print_backtrace(CONTEXT* context) {
    HANDLE process = GetCurrentProcess();
    HANDLE thread = GetCurrentThread();

    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME);
    if (!SymInitialize(process, nullptr, TRUE)) {
        fprintf(stderr, "  (no symbols: SymInitialize failed, error %lu)\n", GetLastError());
    }

    STACKFRAME64 frame{};
    frame.AddrPC.Offset = context->Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = context->Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = context->Rsp;
    frame.AddrStack.Mode = AddrModeFlat;

    // SymFromAddr writes the name into a caller-supplied buffer that has to extend
    // past the struct, hence the raw byte array rather than a plain SYMBOL_INFO.
    alignas(SYMBOL_INFO) char symbol_storage[sizeof(SYMBOL_INFO) + 1024]{};
    SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(symbol_storage);
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = 1024;

    for (int depth = 0; depth < 64; depth++) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, context, nullptr,
                         SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) {
            break;
        }
        if (frame.AddrPC.Offset == 0) {
            break;
        }

        DWORD64 displacement = 0;
        const char* name = "??";
        if (SymFromAddr(process, frame.AddrPC.Offset, &displacement, symbol)) {
            name = symbol->Name;
        }

        IMAGEHLP_LINE64 line{};
        line.SizeOfStruct = sizeof(line);
        DWORD line_displacement = 0;
        if (SymGetLineFromAddr64(process, frame.AddrPC.Offset, &line_displacement, &line)) {
            fprintf(stderr, "  #%-2d 0x%016llx %s+0x%llx (%s:%lu)\n", depth,
                    (unsigned long long)frame.AddrPC.Offset, name, (unsigned long long)displacement,
                    line.FileName, line.LineNumber);
        } else {
            fprintf(stderr, "  #%-2d 0x%016llx %s+0x%llx\n", depth,
                    (unsigned long long)frame.AddrPC.Offset, name, (unsigned long long)displacement);
        }
    }
}

static const char* exception_name(DWORD code) {
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION:      return "ACCESS_VIOLATION";
        case EXCEPTION_ILLEGAL_INSTRUCTION:   return "ILLEGAL_INSTRUCTION";
        case EXCEPTION_INT_DIVIDE_BY_ZERO:    return "INT_DIVIDE_BY_ZERO";
        case EXCEPTION_STACK_OVERFLOW:        return "STACK_OVERFLOW";
        case EXCEPTION_DATATYPE_MISALIGNMENT: return "DATATYPE_MISALIGNMENT";
        case EXCEPTION_IN_PAGE_ERROR:         return "IN_PAGE_ERROR";
        default:                              return "UNKNOWN";
    }
}

static LONG WINAPI unhandled_exception_filter(EXCEPTION_POINTERS* info) {
    const EXCEPTION_RECORD* record = info->ExceptionRecord;

    fprintf(stderr, "\n=================== CRASH ===================\n");
    fprintf(stderr, "Exception: %s (0x%08lx) at 0x%016llx\n", exception_name(record->ExceptionCode),
            record->ExceptionCode, (unsigned long long)(uintptr_t)record->ExceptionAddress);

    if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2) {
        const char* op = record->ExceptionInformation[0] == 0 ? "reading"
                       : record->ExceptionInformation[0] == 1 ? "writing"
                                                              : "executing";
        fprintf(stderr, "Faulted %s address 0x%016llx\n", op,
                (unsigned long long)record->ExceptionInformation[1]);
    }

    fprintf(stderr, "Thread: %lu\nBacktrace:\n", GetCurrentThreadId());
    print_backtrace(info->ContextRecord);
    fprintf(stderr, "=============================================\n");
    fflush(stderr);

    return EXCEPTION_EXECUTE_HANDLER;
}

void register_crash_handler() {
    SetUnhandledExceptionFilter(unhandled_exception_filter);
    std::set_terminate(on_terminate);
}

#else

#include <csignal>
#include <execinfo.h>
#include <unistd.h>

static void signal_handler(int sig, siginfo_t* info, void*) {
    fprintf(stderr, "\n=================== CRASH ===================\n");
    fprintf(stderr, "Signal: %s (%d), faulting address 0x%016llx\n", strsignal(sig), sig,
            (unsigned long long)(uintptr_t)info->si_addr);
    fprintf(stderr, "Backtrace:\n");
    fflush(stderr);

    // backtrace_symbols_fd is async-signal-safe; backtrace_symbols is not.
    void* frames[64];
    int count = backtrace(frames, 64);
    backtrace_symbols_fd(frames, count, STDERR_FILENO);

    fprintf(stderr, "=============================================\n");
    fflush(stderr);

    // Restore the default action and re-raise so the exit status still reports the
    // signal, and any core dump is produced as usual.
    signal(sig, SIG_DFL);
    raise(sig);
}

void register_crash_handler() {
    struct sigaction action{};
    action.sa_sigaction = signal_handler;
    action.sa_flags = SA_SIGINFO | SA_RESETHAND;
    sigemptyset(&action.sa_mask);

    for (int sig : {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT}) {
        sigaction(sig, &action, nullptr);
    }

    std::set_terminate(on_terminate);
}

#endif
