#include "locale/GameText.hpp"

#include "core/Fault.hpp"
#include "core/Log.hpp"
#include "core/ObjectUtil.hpp"
#include "core/Strings.hpp"
#include "locale/Locale.hpp"

#include <Unreal/FString.hpp>
#include <Unreal/NameTypes.hpp>

#include <chrono>
#include <iterator>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace da::gametext
{
    using RC::Unreal::FName;
    using RC::Unreal::FProperty;
    using RC::Unreal::FString;
    using RC::Unreal::UFunction;
    using RC::Unreal::UObject;

    namespace
    {
        std::mutex g_mutex;
        std::unordered_map<std::wstring, std::wstring> g_cache;
        std::wstring g_culture;
        std::chrono::steady_clock::time_point g_cultureAt{};
        std::vector<std::wstring> g_tableIds;

        // The engine's own answer to which language it shows, through the library the game's
        // Blueprints ask.
        std::wstring AskCulture()
        {
            auto* lib = obj::FindObject(L"/Script/Engine.Default__KismetInternationalizationLibrary");
            if (!lib) return {};
            auto* fn = obj::FindFunction(lib, L"GetCurrentLanguage");
            if (!fn) return {};
            std::wstring result;
            obj::Call(lib, fn, nullptr,
                      [&](void* params)
                      {
                          for (auto* prop : fn->ForEachProperty())
                          {
                              if (prop && prop->GetName() == L"ReturnValue") obj::ReadStringAt(params, prop, result);
                          }
                      });
            return str::Trim(result);
        }

        // The string tables the game resolves its text from: the ones its game instance
        // names, else every string table loaded.
        const std::vector<std::wstring>& TableIds()
        {
            if (!g_tableIds.empty()) return g_tableIds;
            if (UObject* instance = obj::GameInstance())
            {
                obj::ForEachArrayElement(instance, obj::FindProperty(instance, L"CachedLocalisationStringTableIDs"),
                                         [&](void* element, FProperty* inner)
                                         {
                                             std::wstring id;
                                             if (obj::ReadStringAt(element, inner, id) && !id.empty() && id != L"None") g_tableIds.push_back(id);
                                         });
            }
            if (g_tableIds.empty())
            {
                for (auto* table : obj::FindAllLive(L"StringTable"))
                {
                    const std::wstring full = obj::FullName(table);
                    const size_t space = full.find(L' ');
                    if (space != std::wstring::npos) g_tableIds.push_back(full.substr(space + 1));
                }
            }
            if (!g_tableIds.empty()) log::Info(L"gametext: string tables {}", str::Join(g_tableIds, L", "));
            return g_tableIds;
        }

        // One key of one table, as the engine's text library resolves it for the game.
        std::wstring FromTable(const std::wstring& tableId, const std::wstring& key)
        {
            auto* lib = obj::FindObject(L"/Script/Engine.Default__KismetTextLibrary");
            if (!lib) return {};
            auto* fn = obj::FindFunction(lib, L"TextFromStringTable");
            if (!fn) return {};
            std::wstring result;
            obj::Call(
                lib, fn,
                [&](void* params)
                {
                    for (auto* prop : fn->ForEachProperty())
                    {
                        if (!prop) continue;
                        if (prop->GetName() == L"TableId")
                            std::construct_at(prop->ContainerPtrToValuePtr<FName>(params), tableId.c_str(), RC::Unreal::FNAME_Add);
                        else if (prop->GetName() == L"Key")
                            std::construct_at(prop->ContainerPtrToValuePtr<FString>(params), key.c_str());
                    }
                },
                [&](void* params)
                {
                    for (auto* prop : fn->ForEachProperty())
                    {
                        if (prop && prop->GetName() == L"ReturnValue") obj::ReadStringAt(params, prop, result);
                    }
                });
            // A key the table does not have comes back as the engine's placeholder for it.
            if (str::StartsWith(result, L"<MISSING")) result.clear();
            return result;
        }
    }

    std::wstring CurrentCulture()
    {
        {
            fault::Guard lock(g_mutex, L"gametext");
            if (!g_culture.empty() && std::chrono::steady_clock::now() - g_cultureAt < std::chrono::seconds(1)) return g_culture;
        }
        const std::wstring culture = AskCulture();
        fault::Guard lock(g_mutex, L"gametext");
        g_culture = culture;
        g_cultureAt = std::chrono::steady_clock::now();
        return g_culture;
    }

    std::wstring CurrentLocale()
    {
        const std::wstring code = locale::CodeForCulture(CurrentCulture());
        if (!code.empty()) return code;
        // The game's own setting, as a second answer: its text language, in the order of its
        // language enumeration.
        static const wchar_t* const kLanguages[] = {L"zh_CHS", L"zh_CHT", L"da_DK", L"nl_NL", L"en_US", L"fi_FI", L"fr_FR", L"de_DE", L"it_IT", L"ja_JP",
                                                    L"ko_KR",  L"nb_NO",  L"pl_PL", L"pt_PT", L"pt_BR", L"ru_RU", L"es_ES", L"es_MX", L"sv_SE", L"tr_TR"};
        UObject* settings = obj::FindFirstLive(L"BatesGameUserSettings");
        int64_t index = -1;
        if (settings && obj::ReadInt(settings, L"CurrentTextLanguage", index) && index >= 0 && index < static_cast<int64_t>(std::size(kLanguages)))
            return kLanguages[index];
        return {};
    }

    std::wstring Resolve(std::wstring_view key)
    {
        if (key.empty()) return {};
        const std::wstring k(key);
        {
            fault::Guard lock(g_mutex, L"gametext");
            const auto it = g_cache.find(k);
            if (it != g_cache.end()) return it->second;
        }
        std::wstring result;
        for (const auto& table : TableIds())
        {
            result = FromTable(table, k);
            if (!result.empty()) break;
        }
        fault::Guard lock(g_mutex, L"gametext");
        g_cache[k] = result;
        return result;
    }

    std::wstring ResolveWithFallback(std::wstring_view key, std::wstring_view fallback)
    {
        const auto resolved = Resolve(key);
        return resolved.empty() ? std::wstring(fallback) : resolved;
    }

    void ClearCache()
    {
        fault::Guard lock(g_mutex, L"gametext");
        g_cache.clear();
        g_culture.clear();
        g_tableIds.clear();
    }
}
