#include "core/AddressCache.hpp"

#include <doctest/doctest.h>

#include <string>
#include <vector>

using da::cache::AddressCache;
using da::cache::Identity;

TEST_CASE("cache: another object at an address answers nothing: WBP_DontMove's BP_Success where a freed function had been, s73 16:40")
{
    // The dispatcher kept "no handlers" for a script function at an address; the game freed it, and
    // the widget's BP_Success came back into the same memory late in a long session. Kept by the
    // address alone, the widget's success ran no handler and the mod said no verdict.
    AddressCache<std::vector<int>> handlers;
    int memory = 0;
    const void* address = &memory;
    const Identity freedOne{11, 7};   // some other function, with no handlers
    const Identity bpSuccess{42, 99}; // BP_Success of WBP_DontMove_C
    handlers.Put(address, freedOne, {});
    CHECK(handlers.Find(address, freedOne) != nullptr);
    CHECK(handlers.Find(address, bpSuccess) == nullptr); // looked up anew, not taken for the freed one
    handlers.Put(address, bpSuccess, {1});
    REQUIRE(handlers.Find(address, bpSuccess) != nullptr);
    CHECK(handlers.Find(address, bpSuccess)->size() == 1);
    CHECK(handlers.Find(address, freedOne) == nullptr);
}

TEST_CASE("cache: what was kept for an object the game frees is forgotten, even if the same one comes back to the same memory")
{
    // A class freed and loaded again under its own name, into its old memory: its properties are new
    // objects, and the old ones may be anything now. Only the free tells it apart; once told, the next
    // lookup is made anew.
    AddressCache<std::wstring> names;
    int memory = 0;
    const Identity sameName{5, 6};
    names.Put(&memory, sameName, L"WBP_DontMove_C");
    CHECK(names.Size() == 1);
    CHECK(names.Forget(&memory));
    CHECK(names.Find(&memory, sameName) == nullptr);
    CHECK_FALSE(names.Forget(&memory)); // nothing left to forget
    CHECK(names.Size() == 0);
}

TEST_CASE("cache: answers for different addresses stand apart, and a new answer replaces the old")
{
    AddressCache<int> cache;
    int a = 0, b = 0;
    cache.Put(&a, Identity{1, 1}, 10);
    cache.Put(&b, Identity{1, 1}, 20);
    REQUIRE(cache.Find(&a, Identity{1, 1}) != nullptr);
    CHECK(*cache.Find(&a, Identity{1, 1}) == 10);
    CHECK(*cache.Find(&b, Identity{1, 1}) == 20);
    cache.Put(&a, Identity{1, 1}, 30);
    CHECK(*cache.Find(&a, Identity{1, 1}) == 30);
    *cache.FindMutable(&b, Identity{1, 1}) = 40;
    CHECK(*cache.Find(&b, Identity{1, 1}) == 40);
    CHECK(cache.FindMutable(&b, Identity{2, 1}) == nullptr);
    cache.Clear();
    CHECK(cache.Size() == 0);
}
