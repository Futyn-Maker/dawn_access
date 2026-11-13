#include "core/ParamReader.hpp"

#include "core/AddressCache.hpp"
#include "core/Fault.hpp"
#include "core/Log.hpp"

#include <mutex>
#include <unordered_map>

namespace da::params
{
    namespace
    {
        std::mutex g_mutex;
        // A function's parameters by name, kept with the function's identity and forgotten when the
        // game frees it (cache::AddressCache): a Blueprint function loaded again has new ones.
        cache::AddressCache<std::unordered_map<std::wstring, FProperty*>> g_cache;
    }

    FProperty* Find(UFunction* function, std::wstring_view name)
    {
        if (!function) return nullptr;
        const cache::Identity identity = obj::IdentityOf(function);
        {
            fault::Guard lock(g_mutex, L"params");
            if (const auto* known = g_cache.Find(function, identity))
            {
                const auto p = known->find(std::wstring(name));
                if (p != known->end()) return p->second;
            }
        }
        FProperty* found = nullptr;
        for (auto* prop : function->ForEachProperty())
        {
            if (!prop) continue;
            if (prop->GetName() == name)
            {
                found = prop;
                break;
            }
        }
        fault::Guard lock(g_mutex, L"params");
        auto* parameters = g_cache.FindMutable(function, identity);
        if (!parameters) parameters = &g_cache.Put(function, identity, {});
        (*parameters)[std::wstring(name)] = found;
        return found;
    }

    bool Forget(const void* function)
    {
        fault::Guard lock(g_mutex, L"params");
        return g_cache.Forget(function);
    }

    bool Bool(FFrame& stack, std::wstring_view name, bool& out)
    {
        auto* prop = Find(stack.Node(), name);
        return prop && stack.Locals() && obj::ReadBoolAt(stack.Locals(), prop, out);
    }

    bool Int(FFrame& stack, std::wstring_view name, int64_t& out)
    {
        auto* prop = Find(stack.Node(), name);
        return prop && stack.Locals() && obj::ReadIntAt(stack.Locals(), prop, out);
    }

    bool Float(FFrame& stack, std::wstring_view name, double& out)
    {
        auto* prop = Find(stack.Node(), name);
        return prop && stack.Locals() && obj::ReadFloatAt(stack.Locals(), prop, out);
    }

    bool String(FFrame& stack, std::wstring_view name, std::wstring& out)
    {
        auto* prop = Find(stack.Node(), name);
        return prop && stack.Locals() && obj::ReadStringAt(stack.Locals(), prop, out);
    }

    bool Object(FFrame& stack, std::wstring_view name, UObject*& out)
    {
        auto* prop = Find(stack.Node(), name);
        return prop && stack.Locals() && obj::ReadObjectAt(stack.Locals(), prop, out);
    }

    void* StructPtr(FFrame& stack, std::wstring_view name)
    {
        auto* prop = Find(stack.Node(), name);
        if (!prop || !stack.Locals()) return nullptr;
        return prop->ContainerPtrToValuePtr<void>(static_cast<void*>(stack.Locals()));
    }

    bool FirstOfType(FFrame& stack, std::wstring_view typeName, std::wstring& out)
    {
        auto* function = stack.Node();
        if (!function || !stack.Locals()) return false;
        for (auto* prop : function->ForEachProperty())
        {
            if (!prop) continue;
            if (prop->HasAnyPropertyFlags(static_cast<uint64_t>(RC::Unreal::EPropertyFlags::CPF_ReturnParm))) continue;
            if (obj::PropertyTypeName(prop) != typeName) continue;
            return obj::ReadStringAt(stack.Locals(), prop, out);
        }
        return false;
    }

    std::wstring Describe(FFrame& stack)
    {
        auto* function = stack.Node();
        if (!function || !stack.Locals()) return {};
        std::wstring out;
        int count = 0;
        for (auto* prop : function->ForEachProperty())
        {
            if (!prop) continue;
            if (++count > 12) break;
            if (!out.empty()) out += L", ";
            out += prop->GetName();
            out += L"=";
            out += obj::ValueToString(prop, prop->ContainerPtrToValuePtr<void>(static_cast<void*>(stack.Locals())), 0);
        }
        return out;
    }
}
