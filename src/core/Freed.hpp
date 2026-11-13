#pragma once
// What the mod has looked up for an engine object is forgotten as the game frees the object.
//
// The game frees Blueprint classes, their functions and their properties as the levels that use
// them stream out, and loads them again, often into the same memory, as levels stream back in.
// A lookup kept by address alone then answers for the object that was there before
// (cache::AddressCache). The engine tells every listener of its object array as it frees an object
// (FUObjectArray's delete listeners, on the game thread or the collector's): the mod's caches by
// address forget that address at once.

namespace da::freed
{
    // Listens to the game's object array; called once the engine is up (on_unreal_init).
    void Install();
    // Stops listening (the mod unloading, or the engine shutting its object array down).
    void Uninstall();
    // How many of the mod's cached lookups the game's frees have taken back so far, for the log.
    unsigned long long ForgottenCount();
}
