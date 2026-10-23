// The fault boundary: a read of dead game memory raises a structured exception, which unwinds
// nothing on the way to the boundary, so a lock taken on that way must be released by the
// boundary itself; a C++ exception passes through it to the catch blocks above.
#include "Support.hpp"
#include "core/Fault.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace
{
    std::uintptr_t g_nowhere = 0; // read at run time, so that the store below is a real one
    std::mutex g_outer;
    std::mutex g_inner;

    void Fault(void*)
    {
        *reinterpret_cast<volatile int*>(g_nowhere) = 1;
    }

    void FaultHoldingInner(void*)
    {
        da::fault::Guard lock(g_inner, L"inner");
        Fault(nullptr);
    }

    void ThrowHoldingInner(void*)
    {
        da::fault::Guard lock(g_inner, L"inner");
        throw std::runtime_error("thrown");
    }

    // An outer lock, a boundary of its own beneath it, and a fault under an inner lock.
    void FaultNested(void*)
    {
        da::fault::Guard lock(g_outer, L"outer");
        da::fault::InvokeLogged(L"inner site", &FaultHoldingInner, nullptr);
    }

    // Whether another thread can take the mutex: true when nobody holds it.
    bool Free(std::mutex& mutex)
    {
        bool free = false;
        std::thread probe(
            [&]
            {
                free = mutex.try_lock();
                if (free) mutex.unlock();
            });
        probe.join();
        return free;
    }
}

TEST_CASE("a fault inside the boundary is contained and described")
{
    CHECK_FALSE(da::fault::Invoke(&Fault, nullptr));
    const auto last = da::fault::Last();
    CHECK(last.code == 0xC0000005ul);
    CHECK(last.kind == 1);
    CHECK(last.accessed == 0);
    CHECK(last.address != 0);
    CHECK(last.released == 0);
    CHECK(da::fault::Held() == 0);
}

TEST_CASE("a lock stranded by a fault is released by the boundary")
{
    CHECK_FALSE(da::fault::Invoke(&FaultHoldingInner, nullptr));
    CHECK(da::fault::Held() == 0);
    CHECK(Free(g_inner));
    const auto last = da::fault::Last();
    CHECK(last.released == 1);
    REQUIRE(last.names[0] != nullptr);
    CHECK(std::wstring(last.names[0]) == L"inner");
    // Nothing is left behind for the next lock of it on this thread.
    CHECK_NOTHROW(da::fault::Guard again(g_inner, L"inner"));
}

TEST_CASE("a lock held above the boundary is left alone")
{
    {
        da::fault::Guard lock(g_outer, L"outer");
        CHECK_FALSE(da::fault::Invoke(&FaultHoldingInner, nullptr));
        CHECK(da::fault::Held() == 1);
        CHECK_FALSE(Free(g_outer));
        CHECK(Free(g_inner));
        CHECK(da::fault::Last().released == 1);
    }
    CHECK(da::fault::Held() == 0);
    CHECK(Free(g_outer));
}

TEST_CASE("an inner boundary releases only what was taken beneath it")
{
    CHECK(da::fault::Invoke(&FaultNested, nullptr));
    CHECK(da::fault::Held() == 0);
    CHECK(Free(g_outer));
    CHECK(Free(g_inner));
    CHECK(da::fault::Last().released == 1);
}

namespace
{
    // More locks held at once than the record has room for, then a fault.
    std::mutex g_many[70];
    void FaultHoldingMany(void*)
    {
        std::vector<std::unique_ptr<da::fault::Guard>> guards;
        for (auto& m : g_many)
            guards.push_back(std::make_unique<da::fault::Guard>(m, L"many"));
        Fault(nullptr);
    }
}

TEST_CASE("locks past the record's room are counted as left locked, the recorded ones released")
{
    CHECK_FALSE(da::fault::Invoke(&FaultHoldingMany, nullptr));
    CHECK(da::fault::Held() == 0);
    const auto last = da::fault::Last();
    CHECK(last.released == 64);
    CHECK(last.unrecorded == 6);
    CHECK(Free(g_many[0]));
    CHECK(Free(g_many[63]));
    // The six past the record stay locked by this thread; they are let go here by hand.
    for (int i = 64; i < 70; ++i)
    {
        CHECK_FALSE(Free(g_many[i]));
        g_many[i].unlock();
    }
}

TEST_CASE("a C++ exception passes through the boundary and unwinds its locks")
{
    CHECK_THROWS_AS(da::fault::Invoke(&ThrowHoldingInner, nullptr), std::runtime_error);
    CHECK(da::fault::Held() == 0);
    CHECK(Free(g_inner));
}

TEST_CASE("the logged boundary names the site, the place and the stranded lock, once in a while")
{
    tests::ClearLog();
    CHECK_FALSE(da::fault::InvokeLogged(L"tests.site", &FaultHoldingInner, nullptr));
    CHECK(tests::LogLinesWith(L"tests.site: memory fault c0000005 writing 0x0 at tests.exe+0x") == 1);
    CHECK(tests::LogLinesWith(L", skipped (1 so far); released the lock 'inner' the fault had stranded") == 1);
    // The second fault at the site within five seconds is counted, not written.
    CHECK_FALSE(da::fault::InvokeLogged(L"tests.site", &Fault, nullptr));
    CHECK(tests::LogLinesWith(L"tests.site: memory fault") == 1);
    // Another site is written at once, without a stranded lock to mention.
    CHECK_FALSE(da::fault::InvokeLogged(L"tests.other", &Fault, nullptr));
    CHECK(tests::LogLinesWith(L"tests.other: memory fault c0000005 writing 0x0 at tests.exe+0x") == 1);
    CHECK(tests::LogLinesWith(L"stranded") == 1);
}
