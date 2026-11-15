#include "core/LevelNames.hpp"

#include "core/ObjectUtil.hpp"
#include "core/Strings.hpp"

#include <map>

namespace da::level
{
    using RC::Unreal::UObject;

    namespace
    {
        std::map<obj::ObjectKey, std::wstring> g_friendlyNames;
    }

    std::wstring CoreName(UObject* actor)
    {
        UObject* core = nullptr;
        std::wstring name;
        if (obj::IsLive(actor) && obj::ReadObject(actor, L"CoreProperties", core) && obj::IsLive(core)) obj::ReadString(core, L"CoreName", name);
        return str::CollapseWhitespace(name);
    }

    std::wstring FriendlyName(UObject* actor)
    {
        if (!obj::IsLive(actor)) return {};
        if (const auto known = g_friendlyNames.find(obj::KeyOf(actor)); known != g_friendlyNames.end()) return known->second;
        std::wstring name;
        UObject* library = obj::FindObject(L"/Script/BMCommonRuntime.Default__LADFunctionLibrary");
        auto* find = library ? obj::FindFunction(library, L"FindLevelDatabaseByActor") : nullptr;
        UObject* database = nullptr;
        if (find)
        {
            obj::Call(
                library, find,
                [&](void* params)
                {
                    for (auto* prop : find->ForEachProperty())
                    {
                        if (prop && prop->GetName() == L"Actor") *static_cast<UObject**>(obj::ValuePtrAt(params, prop)) = actor;
                    }
                },
                [&](void* params)
                {
                    for (auto* prop : find->ForEachProperty())
                    {
                        if (prop && prop->GetName() == L"ReturnValue") obj::ReadObjectAt(params, prop, database);
                    }
                });
        }
        auto* get = obj::IsLive(database) ? obj::FindFunction(database, L"GetActorData") : nullptr;
        if (get)
        {
            obj::Call(
                database, get,
                [&](void* params)
                {
                    for (auto* prop : get->ForEachProperty())
                    {
                        if (!prop) continue;
                        const auto n = prop->GetName();
                        if (n == L"Actor")
                            *static_cast<UObject**>(obj::ValuePtrAt(params, prop)) = actor;
                        else if (n == L"bCheckSublevels")
                            *static_cast<bool*>(obj::ValuePtrAt(params, prop)) = true;
                    }
                },
                [&](void* params)
                {
                    for (auto* prop : get->ForEachProperty())
                    {
                        if (prop && prop->GetName() == L"OutData")
                            obj::ReadStringAt(obj::ValuePtrAt(params, prop), obj::StructMember(prop, L"FriendlyName"), name);
                    }
                });
        }
        name = str::CollapseWhitespace(name);
        if (g_friendlyNames.size() > 512) std::erase_if(g_friendlyNames, [](const auto& item) { return !obj::IsSame(item.first); });
        g_friendlyNames[obj::KeyOf(actor)] = name;
        return name;
    }

    std::wstring PlacingLevel(UObject* actor)
    {
        if (!obj::IsLive(actor)) return {};
        UObject* level = obj::Outer(actor);
        if (!level || obj::ClassName(level) != L"Level") return {};
        UObject* world = obj::Outer(level);
        if (!world || obj::ClassName(world) != L"World") return {};
        return obj::ObjectName(world);
    }
}
