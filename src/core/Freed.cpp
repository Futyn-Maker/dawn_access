#include "core/Freed.hpp"

#include "core/Log.hpp"
#include "core/ObjectUtil.hpp"
#include "core/ParamReader.hpp"
#include "hooks/HookDispatcher.hpp"

#include <Unreal/UObjectArray.hpp>

#include <atomic>
#include <cstddef>

namespace da::freed
{
    namespace
    {
        std::atomic<unsigned long long> g_forgotten{0};
        std::atomic<bool> g_listening{false};

        // The engine's FUObjectDeleteListener: NotifyUObjectDeleted for every object it frees,
        // OnUObjectArrayShutdown when the array goes, and GetAllocatedSize (the engine's own third
        // virtual, asked for memory reports) answered so that the table matches the engine's.
        class Listener final : public RC::Unreal::FUObjectDeleteListener
        {
        public:
            void NotifyUObjectDeleted(const RC::Unreal::UObjectBase* object, int32_t) override
            {
                if (!object || !g_listening.load(std::memory_order_relaxed)) return;
                // Only the address is used: the object is being torn down and is not read.
                const void* address = object;
                const bool names = obj::Forget(address);
                const bool parameters = params::Forget(address);
                const bool handlers = hooks::Forget(address);
                if (names || parameters || handlers) g_forgotten.fetch_add(1, std::memory_order_relaxed);
            }

            void OnUObjectArrayShutdown() override
            {
                g_listening = false;
                RC::Unreal::UObjectArray::RemoveUObjectDeleteListener(this);
            }

            virtual size_t GetAllocatedSize() const
            {
                return 0;
            }
        };

        Listener g_listener;
    }

    void Install()
    {
        if (g_listening) return;
        try
        {
            RC::Unreal::UObjectArray::AddUObjectDeleteListener(&g_listener);
            g_listening = true;
            log::Info(L"objects: the mod's lookups by address are forgotten as the game frees the objects");
        }
        catch (const std::exception&)
        {
            log::Error(L"objects: the game's object array takes no listener; lookups by address are checked by their names alone");
        }
    }

    void Uninstall()
    {
        if (!g_listening.exchange(false)) return;
        RC::Unreal::UObjectArray::RemoveUObjectDeleteListener(&g_listener);
    }

    unsigned long long ForgottenCount()
    {
        return g_forgotten.load(std::memory_order_relaxed);
    }
}
