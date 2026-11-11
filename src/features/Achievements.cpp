#include "features/Achievements.hpp"

#include "features/AchievementRules.hpp"

#include "core/GameThread.hpp"
#include "core/Log.hpp"
#include "core/ObjectUtil.hpp"
#include "core/Strings.hpp"
#include "locale/GameText.hpp"
#include "locale/Locale.hpp"
#include "speech/Speech.hpp"

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace da::features
{
    namespace
    {
        // Steam's flat API, as steam_api64.dll exports it (Steamworks SDK 1.53, which the game ships in
        // Engine/Binaries/ThirdParty/Steamworks/Steamv153): ISteamUserStats's own functions, each taking
        // the interface first.
        using UserStatsFn = void* (*)();
        using NumAchievementsFn = uint32_t (*)(void*);
        using AchievementNameFn = const char* (*)(void*, uint32_t);
        using GetAchievementFn = bool (*)(void*, const char*, bool*);
        using DisplayAttributeFn = const char* (*)(void*, const char*, const char*);
        using AppsFn = void* (*)();
        using GameLanguageFn = const char* (*)(void*);

        struct SteamApi
        {
            UserStatsFn userStats = nullptr;
            NumAchievementsFn count = nullptr;
            AchievementNameFn name = nullptr;
            GetAchievementFn get = nullptr;
            DisplayAttributeFn attribute = nullptr;
            AppsFn apps = nullptr;
            GameLanguageFn language = nullptr;
        };
        SteamApi g_api;
        bool g_resolved = false;
        double g_lookedAt = -1.0;     // when steam_api64.dll was last looked for
        bool g_absentLogged = false;
        achieve::Watch g_watch;

        // The library is loaded by the game's online subsystem, a moment into the game's start.
        bool Resolve(double now)
        {
            if (g_resolved) return true;
            if (g_lookedAt >= 0.0 && now - g_lookedAt < 5.0) return false;
            g_lookedAt = now;
            HMODULE module = GetModuleHandleW(L"steam_api64.dll");
            if (!module)
            {
                if (!g_absentLogged)
                {
                    g_absentLogged = true;
                    log::Info(L"achievements: Steam's API is not in the game yet; looked for every 5 s");
                }
                return false;
            }
            SteamApi api;
            api.userStats = reinterpret_cast<UserStatsFn>(GetProcAddress(module, "SteamAPI_SteamUserStats_v012"));
            api.count = reinterpret_cast<NumAchievementsFn>(GetProcAddress(module, "SteamAPI_ISteamUserStats_GetNumAchievements"));
            api.name = reinterpret_cast<AchievementNameFn>(GetProcAddress(module, "SteamAPI_ISteamUserStats_GetAchievementName"));
            api.get = reinterpret_cast<GetAchievementFn>(GetProcAddress(module, "SteamAPI_ISteamUserStats_GetAchievement"));
            api.attribute = reinterpret_cast<DisplayAttributeFn>(GetProcAddress(module, "SteamAPI_ISteamUserStats_GetAchievementDisplayAttribute"));
            api.apps = reinterpret_cast<AppsFn>(GetProcAddress(module, "SteamAPI_SteamApps_v008"));
            api.language = reinterpret_cast<GameLanguageFn>(GetProcAddress(module, "SteamAPI_ISteamApps_GetCurrentGameLanguage"));
            if (!api.userStats || !api.count || !api.name || !api.get || !api.attribute)
            {
                if (!g_absentLogged)
                {
                    g_absentLogged = true;
                    log::Info(L"achievements: steam_api64.dll lacks the functions of ISteamUserStats this mod asks; achievements are not said");
                }
                return false;
            }
            g_api = api;
            g_resolved = true;
            log::Info(L"achievements: Steam's API found; its achievements are watched");
            return true;
        }

        std::wstring Attribute(void* stats, const std::string& id, const char* key)
        {
            const char* value = g_api.attribute(stats, id.c_str(), key);
            return value ? str::Trim(str::Utf8ToWide(value)) : std::wstring();
        }

        // The game's own words for an achievement, in the language the game shows its text in.
        std::wstring GameWords(const std::string& id, achieve::Field field)
        {
            for (const auto& key : achieve::TextKeys(id, field))
            {
                auto words = str::Trim(gametext::Resolve(key));
                if (!words.empty()) return words;
            }
            return {};
        }

        void PollImpl()
        {
            // Once a second: an unlock is said within it, as Steam's toast comes up.
            if (gamethread::FrameCount() % 60 != 0) return;
            const double now = gamethread::NowSeconds();
            if (!Resolve(now)) return;
            void* stats = g_api.userStats();
            if (!stats) return;
            // Nothing until Steam has the player's stats: it counts none before.
            const uint32_t count = g_api.count(stats);
            if (count == 0) return;
            std::vector<achieve::State> states;
            states.reserve(count);
            bool complete = true;
            for (uint32_t i = 0; i < count; ++i)
            {
                const char* id = g_api.name(stats, i);
                if (!id || !*id)
                {
                    complete = false;
                    continue;
                }
                bool achieved = false;
                if (!g_api.get(stats, id, &achieved))
                {
                    complete = false;
                    continue;
                }
                states.push_back(achieve::State{id, achieved});
            }
            const bool seeded = g_watch.Seeded();
            const auto fresh = g_watch.Update(states, complete);
            if (!seeded && g_watch.Seeded())
            {
                std::wstring language = L"unknown";
                if (g_api.apps && g_api.language)
                {
                    if (void* apps = g_api.apps())
                    {
                        if (const char* code = g_api.language(apps)) language = str::Utf8ToWide(code);
                    }
                }
                log::Info(L"achievements: Steam lists {}, {} of them unlocked already; its words are in {}", count, g_watch.Achieved(), language);
            }
            for (const auto& id : fresh)
            {
                // In the game's language, the mod's own (Steam's are English whatever the player's).
                const auto gameName = GameWords(id, achieve::Field::Name);
                const auto gameDescription = GameWords(id, achieve::Field::Description);
                const auto name = achieve::Pick(gameName, Attribute(stats, id, "name"), str::Utf8ToWide(id));
                const auto description = achieve::Pick(gameDescription, Attribute(stats, id, "desc"));
                const auto words = str::JoinSentences({locale::Mod(L"achievement.unlocked", name), description});
                log::Info(L"achievements: {} unlocked: \"{}\" ({})", str::Utf8ToWide(id), words,
                          !gameName.empty() && !gameDescription.empty() ? L"the game's words" : L"Steam's words where the game has none");
                speech::Announce(words);
            }
        }

        void Poll(float)
        {
            obj::SafeInvokeLogged(L"achievements.PollImpl", [](void*) { PollImpl(); }, nullptr);
        }
    }

    void AchievementsFeature::Install()
    {
        gamethread::AddPoller(L"achievements", &Poll);
    }
}
