#include "core/Fault.hpp"

#include "core/Log.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <chrono>
#include <format>
#include <map>
#include <string>
#include <utility>

namespace da::fault
{
    namespace
    {
        constexpr unsigned kMaxHeld = 64;
        constexpr unsigned long kCppException = 0xE06D7363; // the code every C++ throw is raised with

        struct HeldLock
        {
            std::mutex* mutex;
            const wchar_t* name;
        };
        thread_local HeldLock t_held[kMaxHeld];
        thread_local unsigned t_depth = 0;
        thread_local Record t_last;

        // Called while the fault is being dispatched, before anything is abandoned: it only
        // notes what happened. A C++ exception is left to the catch blocks above the boundary.
        int Filter(unsigned long code, EXCEPTION_POINTERS* info)
        {
            if (code == kCppException) return EXCEPTION_CONTINUE_SEARCH;
            Record record;
            record.code = code;
            if (info && info->ExceptionRecord)
            {
                const EXCEPTION_RECORD& r = *info->ExceptionRecord;
                record.address = reinterpret_cast<std::uintptr_t>(r.ExceptionAddress);
                if ((code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_IN_PAGE_ERROR) && r.NumberParameters >= 2)
                {
                    record.kind = static_cast<int>(r.ExceptionInformation[0]);
                    record.accessed = static_cast<std::uintptr_t>(r.ExceptionInformation[1]);
                }
            }
            t_last = record;
            return EXCEPTION_EXECUTE_HANDLER;
        }

        // Unlocks what the abandoned frames beneath `depth` still hold, innermost first, and
        // keeps their names for the report. The thread that locked them is this one, so the
        // unlock is its own to make; the guards that would have made it are gone with their
        // frames.
        void Release(unsigned depth)
        {
            unsigned released = 0;
            while (t_depth > depth)
            {
                --t_depth;
                if (t_depth < kMaxHeld)
                {
                    HeldLock& held = t_held[t_depth];
                    if (released < Record::kNames) t_last.names[released] = held.name;
                    if (held.mutex) held.mutex->unlock();
                    held = {};
                }
                ++released;
            }
            t_last.released = released;
        }

        // The structured-exception frame lives in a function of its own, with no C++ object
        // in it, as the compiler requires.
        bool Run(void (*fn)(void*), void* context, unsigned depth)
        {
            __try
            {
                fn(context);
                return true;
            }
            __except (Filter(GetExceptionCode(), GetExceptionInformation()))
            {
                Release(depth);
                return false;
            }
        }

        // The module the address lies in and the offset within it, for the symbol file.
        std::wstring Where(std::uintptr_t address)
        {
            HMODULE module = nullptr;
            if (address &&
                GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(address),
                                   &module) &&
                module)
            {
                wchar_t path[MAX_PATH] = {};
                const DWORD n = GetModuleFileNameW(module, path, MAX_PATH);
                std::wstring name(path, path + n);
                const auto slash = name.find_last_of(L"\\/");
                if (slash != std::wstring::npos) name.erase(0, slash + 1);
                return std::format(L"{}+0x{:x}", name, address - reinterpret_cast<std::uintptr_t>(module));
            }
            return std::format(L"0x{:x}", address);
        }

        std::wstring Describe(const Record& r)
        {
            std::wstring text = std::format(L"{:08x}", r.code);
            if (r.kind == 0)
                text += std::format(L" reading 0x{:x}", r.accessed);
            else if (r.kind == 1)
                text += std::format(L" writing 0x{:x}", r.accessed);
            else if (r.kind == 8)
                text += std::format(L" executing 0x{:x}", r.accessed);
            return text + L" at " + Where(r.address);
        }

        std::wstring Stranded(const Record& r)
        {
            if (r.released == 0) return {};
            std::wstring text = r.released == 1 ? L"; released the lock" : L"; released the locks";
            for (unsigned i = 0; i < r.released && i < Record::kNames; ++i)
                text += std::format(L" '{}'", r.names[i] ? r.names[i] : L"?");
            if (r.released > Record::kNames) text += L" ...";
            return text + L" the fault had stranded";
        }
    }

    Guard::Guard(std::mutex& mutex, const wchar_t* name) : m_mutex(mutex)
    {
        mutex.lock();
        if (t_depth < kMaxHeld) t_held[t_depth] = HeldLock{&mutex, name};
        ++t_depth;
    }

    Guard::~Guard()
    {
        if (t_depth > 0) --t_depth;
        m_mutex.unlock();
    }

    bool Invoke(void (*fn)(void*), void* context)
    {
        return Run(fn, context, t_depth);
    }

    bool InvokeLogged(const wchar_t* site, void (*fn)(void*), void* context)
    {
        if (Invoke(fn, context)) return true;
        const Record record = t_last;
        static std::map<std::wstring, std::pair<int, double>> faults;
        static std::mutex faultsMutex;
        int count = 0;
        bool report = false;
        {
            Guard lock(faultsMutex, L"fault.sites");
            auto& [seen, reportedAt] = faults[site];
            count = ++seen;
            const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
            if (reportedAt == 0.0 || now - reportedAt > 5.0)
            {
                reportedAt = now;
                report = true;
            }
        }
        if (report) log::Error(L"{}: memory fault {}, skipped ({} so far){}", site, Describe(record), count, Stranded(record));
        return false;
    }

    Record Last()
    {
        return t_last;
    }

    unsigned Held()
    {
        return t_depth;
    }
}
