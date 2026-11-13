#pragma once
// Answers kept by the address of an engine object (a class, a function), with no engine in it.
//
// The game frees Blueprint classes as the levels that use them go and loads them again when
// they come back, and a new class or function can take the memory an old one had. An answer
// kept by address alone is then the old object's: the hook dispatcher, which kept the handlers
// of every script function it had seen by its address, ran none for WBP_DontMove's BP_Success
// once the widget's class had come back late in a long session (Sam's two Stay Calm runs in the
// lodge on 2026-11-13, research/sessions/s73-play.log, 16:40:27 and 16:43:02: the widget played
// its success, the mod heard nothing). So an answer is kept with the identity of the object it
// was taken for (its name, and its outer's), is no answer for another object at the same
// address, and is forgotten when the game frees the object (core/Freed).

#include <cstdint>
#include <unordered_map>
#include <utility>

namespace da::cache
{
    // An object's identity beyond its address: the tokens of its name and of its outer's name.
    struct Identity
    {
        uint64_t name = 0;
        uint64_t outer = 0;
        bool operator==(const Identity&) const = default;
    };

    template <class Value>
    class AddressCache
    {
    public:
        // The answer kept for the object at this address, when it was kept for an object of this
        // identity; null otherwise.
        const Value* Find(const void* address, const Identity& identity) const
        {
            const auto it = m_slots.find(address);
            if (it == m_slots.end() || !(it->second.identity == identity)) return nullptr;
            return &it->second.value;
        }

        Value* FindMutable(const void* address, const Identity& identity)
        {
            const auto it = m_slots.find(address);
            if (it == m_slots.end() || !(it->second.identity == identity)) return nullptr;
            return &it->second.value;
        }

        // Keeps an answer for the object at this address, in place of anything kept there before.
        Value& Put(const void* address, const Identity& identity, Value value)
        {
            Slot& slot = m_slots[address];
            slot.identity = identity;
            slot.value = std::move(value);
            return slot.value;
        }

        // The game has freed the object at this address: true when an answer was kept for it.
        bool Forget(const void* address)
        {
            return m_slots.erase(address) > 0;
        }

        void Clear()
        {
            m_slots.clear();
        }
        size_t Size() const
        {
            return m_slots.size();
        }

    private:
        struct Slot
        {
            Identity identity;
            Value value{};
        };
        std::unordered_map<const void*, Slot> m_slots;
    };
}
