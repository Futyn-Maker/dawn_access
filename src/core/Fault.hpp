#pragma once
// The boundary against memory faults. The pollers and hooks read live game objects, which the
// game can tear down between two frames, and a read of a dead one raises a structured
// exception rather than a C++ one. The mod is compiled for C++ exceptions only, so nothing in
// the frames between the boundary and the faulting line is unwound: a mutex locked in one of
// them would stay locked for good, and the next lock of it on the same thread would throw
// "resource deadlock would occur" from every poller that touches it, which is a mod that
// still answers its hotkeys but voices nothing of the game. So every lock the mod takes goes
// through the guard here, which keeps a per-thread record of what the thread holds, and the
// boundary releases what an abandoned frame left locked and says so in the log: a fault
// costs one skipped frame.

#include <cstdint>
#include <mutex>

namespace da::fault
{
    // A scoped lock that the boundary can see: the mod's replacement for std::lock_guard.
    // `name` labels the lock in the log when a fault strands it; a string literal.
    class Guard
    {
    public:
        Guard(std::mutex& mutex, const wchar_t* name);
        ~Guard();
        Guard(const Guard&) = delete;
        Guard& operator=(const Guard&) = delete;

    private:
        std::mutex& m_mutex;
    };

    // What the last fault on this thread was, for the log and the tests.
    struct Record
    {
        static constexpr unsigned kNames = 4;
        unsigned long code = 0;            // the exception code (c0000005 for an access violation)
        std::uintptr_t address = 0;        // where the faulting instruction was
        std::uintptr_t accessed = 0;       // the address it touched, when known
        int kind = -1;                     // 0 a read, 1 a write, 8 an execution, -1 unknown
        unsigned released = 0;             // how many stranded locks the boundary released
        const wchar_t* names[kNames] = {}; // the first of them, innermost first
    };

    // Runs `fn(context)` under the boundary. True when it returned; false when it faulted, in
    // which case the locks taken beneath the boundary have been released and Last() tells
    // what happened. A C++ exception passes through unchanged, to the catch blocks above.
    // `fn` must be a captureless function; keep C++ objects inside it, not around it.
    bool Invoke(void (*fn)(void*), void* context);
    // The same, with the fault written to the log (where it was, what it did, which locks it
    // stranded), at most once in five seconds per site, so that a poller dying on every frame
    // shows in the log instead of failing in silence.
    bool InvokeLogged(const wchar_t* site, void (*fn)(void*), void* context);

    Record Last();
    // How many locks this thread holds through the guard.
    unsigned Held();
}
