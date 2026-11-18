// Dawn Access — screen-reader accessibility mod for Until Dawn (UE4SS C++ mod).
// Entry point: UE4SS loads Mods\DawnAccess\dlls\main.dll and calls start_mod().

#include "core/Config.hpp"
#include "core/Freed.hpp"
#include "core/GameThread.hpp"
#include "core/Greeting.hpp"
#include "core/Log.hpp"
#include "core/ObjectUtil.hpp"
#include "core/Strings.hpp"
#include "core/Symbols.hpp"
#include "diag/Audit.hpp"
#include "diag/Diagnostics.hpp"
#include "diag/SaveCopies.hpp"
#include "features/Achievements.hpp"
#include "features/Choices.hpp"
#include "features/Credits.hpp"
#include "features/DontMove.hpp"
#include "features/Exploration.hpp"
#include "features/Feature.hpp"
#include "features/Films.hpp"
#include "features/Hud.hpp"
#include "features/Interactions.hpp"
#include "features/Menus.hpp"
#include "features/Prompts.hpp"
#include "features/Qte.hpp"
#include "features/Shooting.hpp"
#include "features/Subtitles.hpp"
#include "features/Totems.hpp"
#include "hooks/HookDispatcher.hpp"
#include "hotkeys/Hotkeys.hpp"
#include "input/InputNames.hpp"
#include "locale/GameText.hpp"
#include "locale/Locale.hpp"
#include "speech/Outputs.hpp"
#include "speech/Sounds.hpp"
#include "speech/Speech.hpp"
#include "watch/GameState.hpp"
#include "watch/Narration.hpp"
#include "watch/Watchers.hpp"

#include <Mod/CppUserModBase.hpp>
#include <Unreal/Hooks.hpp>

#include <windows.h>

#include <filesystem>
#include <memory>

// DA_VERSION_STRING comes from the VERSION file at the root of the repository, through CMake.

using namespace RC;

namespace
{
    std::wstring ModuleDirectory()
    {
        HMODULE module = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&ModuleDirectory),
                           &module);
        wchar_t buffer[MAX_PATH * 2]{};
        GetModuleFileNameW(module, buffer, static_cast<DWORD>(std::size(buffer)));
        return std::filesystem::path(buffer).parent_path().wstring();
    }

    // The timestamp the linker gave this main.dll, as a crash dump names the module by it and as the
    // kept symbols are named (build\symbols\<timestamp>, tools\exe\crash.py): the version alone stays
    // the same across test builds.
    DWORD ModuleTimestamp()
    {
        HMODULE module = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&ModuleDirectory),
                           &module);
        if (!module) return 0;
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(reinterpret_cast<const uint8_t*>(module) + dos->e_lfanew);
        return nt->Signature == IMAGE_NT_SIGNATURE ? nt->FileHeader.TimeDateStamp : 0;
    }

    // What the game's settings and its UI framework say about the session, for the log.
    void LogGameState()
    {
        if (auto* settings = da::obj::FindFirstLive(L"BatesGameUserSettings"))
        {
            int64_t text = -1, speech = -1, subtitles = -1;
            bool narration = false;
            da::obj::ReadInt(settings, L"CurrentTextLanguage", text);
            da::obj::ReadInt(settings, L"CurrentSpeechLanguage", speech);
            da::obj::ReadInt(settings, L"CurrentSubtitleLanguage", subtitles);
            da::obj::ReadBool(settings, L"bIsNarrationEnabled", narration);
            da::log::Info(L"self-check: game languages text {} speech {} subtitles {}; narration {}", text, speech, subtitles, narration ? L"on" : L"off");
        }
        if (auto* ui = da::obj::FindFirstLive(L"BMUISubsystem"))
        {
            // The input type and the gamepad vendor are given out by functions; the state
            // and the input mode are not reflected at all.
            int64_t inputType = -1;
            da::obj::CallReturn(ui, L"GetCurrentInputType",
                                [&](void* params, da::obj::FProperty* returnValue)
                                {
                                    if (returnValue) da::obj::ReadIntAt(params, returnValue, inputType);
                                });
            da::log::Info(L"self-check: UI input type {} gamepad vendor \"{}\"", inputType, da::obj::CallForText(ui, L"GetCurrentGamepadVendor"));
        }
    }
}

namespace
{
    // The engine's signs that the game is going away, read from its globals (core/Symbols); none
    // when the build is not the one the table describes.
    da::outputs::ClosingSigns GameClosingSigns()
    {
        da::outputs::ClosingSigns signs;
        if (const auto* exit = static_cast<const volatile bool*>(da::sym::Address(L"GIsRequestingExit"))) signs.exitRequested = *exit;
        if (const auto* fatal = static_cast<const volatile bool*>(da::sym::Address(L"GIsCriticalError"))) signs.criticalError = *fatal;
        if (const auto* calls = static_cast<const volatile int32_t*>(da::sym::Address(L"ReportCrash.calls"))) signs.crashReports = *calls;
        return signs;
    }
}

class DawnAccessMod : public CppUserModBase
{
public:
    DawnAccessMod()
    {
        ModName = STR("DawnAccess");
        ModVersion = da::str::Utf8ToWide(DA_VERSION_STRING);
        ModDescription = STR("Screen-reader accessibility for Until Dawn (NVDA, JAWS, Narrator, SAPI).");
        ModAuthors = STR("Futyn-Maker");
        ModIntendedSDKVersion = STR("3.0.1");
    }

    ~DawnAccessMod() override
    {
        da::freed::Uninstall();
        da::hotkeys::Uninstall();
        da::input::ForgetWalkKeys();
        da::outputs::Stop();
        da::log::Shutdown();
    }

    auto on_unreal_init() -> void override
    {
        // Mods\DawnAccess\dlls\main.dll -> Mods\DawnAccess
        const std::wstring dllDir = ModuleDirectory();
        m_modDir = std::filesystem::path(dllDir).parent_path().wstring();

        std::wstring configError;
        auto settings = da::cfg::LoadSettings(m_modDir + L"\\DawnAccess.ini", &configError);
        da::cfg::Set(settings);
        da::log::Level level = da::log::Level::Info;
        da::log::ParseLevel(settings.logLevel, level);
        da::log::Init(m_modDir + L"\\DawnAccess.log", settings.logFile, level);
        da::diag::SetModDir(m_modDir);

        da::log::Info(L"DawnAccess {} (build {:08x}) starting; mod dir {}", ModVersion, ModuleTimestamp(), m_modDir);
        if (!configError.empty()) da::log::Error(L"config: {} (defaults in use)", configError);

        da::speech::SetPreferSapi(settings.preferSapi);
        // The speech library sits beside main.dll and is loaded from there, so that the mod's
        // copy of it is the mod's own. It finds the screen reader and creates the SAPI voice,
        // and says in the log which of them answered.
        if (!da::outputs::Start(dllDir + L"\\prism.dll"))
        {
            da::log::Error(L"Nothing can be spoken: neither a screen reader nor SAPI answered. prism.dll belongs beside main.dll in Mods\\DawnAccess\\dlls.");
            MessageBeep(MB_ICONERROR);
        }
        da::speech::Init();
        da::sounds::Init(settings.soundVolume);
        // The mod falls silent the moment the game goes away (speech::rules::Closing). The build's
        // check is decided here, before the speech thread first asks the symbols.
        da::sym::Running();
        da::outputs::WatchClosing(&GameClosingSigns);

        // English strings first; the game's language is applied once the engine reports it.
        const std::wstring forcedLanguage = da::str::EqualsNoCase(settings.language, L"auto") ? L"" : settings.language;
        da::locale::LoadTables(m_modDir + L"\\lang", forcedLanguage.empty() ? L"en_US" : forcedLanguage);

        da::gamethread::Install();
        // What the player presses is watched so that only their own actions cut an
        // utterance short.
        da::input::InstallActivityTracker();
        da::hooks::Install();
        da::freed::Install();
        da::watch::Install();
        da::narration::Install();
        da::gamestate::Install();
        da::hotkeys::Install();
        da::diag::InstallCommandFile();
        da::diag::InstallFrameTimer();
        da::diag::InstallFirstLaunch();
        da::diag::InstallUnreadAudit();
        da::diag::InstallSaveCopies();
        // The features are registered now and installed once the greeting has been said, so
        // that nothing of the screen is read before it.
        da::features::Register(std::make_unique<da::features::MenusFeature>());
        da::features::Register(std::make_unique<da::features::SubtitlesFeature>());
        da::features::Register(std::make_unique<da::features::HudFeature>());
        da::features::Register(std::make_unique<da::features::PromptsFeature>());
        da::features::Register(std::make_unique<da::features::ChoicesFeature>());
        da::features::Register(std::make_unique<da::features::QteFeature>());
        da::features::Register(std::make_unique<da::features::DontMoveFeature>());
        da::features::Register(std::make_unique<da::features::ExplorationFeature>());
        da::features::Register(std::make_unique<da::features::TotemsFeature>());
        da::features::Register(std::make_unique<da::features::ShootingFeature>());
        da::features::Register(std::make_unique<da::features::InteractionsFeature>());
        da::features::Register(std::make_unique<da::features::CreditsFeature>());
        da::features::Register(std::make_unique<da::features::FilmsFeature>());
        da::features::Register(std::make_unique<da::features::AchievementsFeature>());

        Unreal::Hook::FCallbackOptions options{};
        options.bReadonly = true;
        options.OwnerModName = STR("DawnAccess");
        options.HookName = STR("LoadMap");
        Unreal::Hook::RegisterLoadMapPostCallback(
            [](Unreal::Hook::TCallbackIterationData<bool>&, Unreal::UEngine*, Unreal::FWorldContext&, Unreal::FURL url, Unreal::UPendingNetGame*,
               Unreal::FString&)
            {
                // The map's path names the act, for the copies of the game's saves (diag::NoteMap).
                std::wstring map = url.Map.Len() > 0 && *url.Map ? std::wstring(*url.Map, static_cast<size_t>(url.Map.Len())) : std::wstring();
                da::gamethread::Post(
                    [map]()
                    {
                        da::log::Info(L"map loaded; resetting caches");
                        da::diag::NoteMap(map);
                        da::obj::ResetCaches();
                        da::watch::Reset();
                        da::features::ResetMenus();
                        da::features::ResetSubtitles();
                        da::features::ResetFilms();
                        da::gamestate::Reset();
                        da::input::InvalidateCache();
                    });
            },
            options);

        // Startup: wait for the game's language, load the matching table and greet, then
        // follow the language for the rest of the session.
        da::gamethread::AddPoller(L"startup", [this, forcedLanguage](float) { Startup(forcedLanguage); });
        da::log::Info(L"initialization done; waiting for the game language");
    }

private:
    void Startup(const std::wstring& forcedLanguage)
    {
        const double now = da::gamethread::NowSeconds();
        if (now - m_lastLocaleProbe < 0.5) return;
        m_lastLocaleProbe = now;

        const std::wstring culture = da::gametext::CurrentCulture();
        const std::wstring gameLocale = da::locale::CodeForCulture(culture);
        if (m_started)
        {
            FollowLocale(forcedLanguage, gameLocale);
            return;
        }
        // The engine starts on the system's language and applies the game's own setting a
        // little later, so an answer is taken only once it has held for two seconds.
        if (gameLocale != m_candidateLocale)
        {
            m_candidateLocale = gameLocale;
            m_candidateSince = now;
        }
        if (gameLocale.empty() ? now < 8.0 : now - m_candidateSince < 2.0) return;
        m_started = true;
        m_gameLocale = gameLocale;

        const std::wstring language = !forcedLanguage.empty() ? forcedLanguage : (gameLocale.empty() ? L"en_US" : gameLocale);
        da::locale::LoadTables(m_modDir + L"\\lang", language);
        da::log::Info(L"game culture: {}; mod language: {}", culture.empty() ? L"<unknown>" : culture, da::locale::CurrentCode());
        // The SAPI voice follows the language of what is said.
        da::outputs::SelectVoiceFor(da::locale::CurrentCode());

        // Self-check: game string resolution, control scheme, key naming.
        da::log::Info(L"self-check: BATES_MENU_QUIT = \"{}\"", da::gametext::Resolve(L"BATES_MENU_QUIT"));
        da::log::Info(L"self-check: control scheme {}", da::input::SchemeName(da::input::CurrentScheme()));
        da::input::ShareGamepadReading();
        da::log::Info(L"self-check: IA_UI_Back = \"{}\"", da::input::KeyForAction(L"IA_UI_Back"));
        da::log::Info(L"self-check: script handlers {}", da::hooks::ScriptHandlerCount());
        LogGameState();

        {
            const std::wstring output = da::speech::OutputName();
            // What the mod runs with belongs in the log either way; briefly the player is told
            // only that it is there, and at either level when the game is a build it does not know.
            da::greeting::Facts facts;
            facts.speakOnLoad = da::cfg::Get().speakOnLoad;
            facts.detailed = da::cfg::Detailed();
            facts.version = ModVersion;
            facts.reader = output.empty() ? da::locale::Mod(L"greeting.noreader") : output;
            facts.language = da::locale::CurrentCode();
            facts.buildKnown = da::sym::BuildKnown();
            const std::wstring words = da::greeting::Words(facts);
            if (!words.empty()) da::speech::Announce(words);
        }
        da::features::InstallAll();
        da::log::Info(L"ready");
    }

    // The language can still change after the start, so the mod follows it for the whole
    // session: the game's strings are resolved anew, and the mod's tables and voice change.
    void FollowLocale(const std::wstring& forcedLanguage, const std::wstring& gameLocale)
    {
        if (gameLocale.empty() || gameLocale == m_gameLocale) return;
        da::log::Info(L"game language changed: {} -> {}", m_gameLocale.empty() ? L"<unknown>" : m_gameLocale, gameLocale);
        m_gameLocale = gameLocale;
        da::gametext::ClearCache();
        da::input::InvalidateCache();
        if (!forcedLanguage.empty()) return;
        da::locale::LoadTables(m_modDir + L"\\lang", gameLocale);
        da::outputs::SelectVoiceFor(da::locale::CurrentCode());
        da::log::Info(L"mod language: {}", da::locale::CurrentCode());
    }

    std::wstring m_modDir;
    std::wstring m_gameLocale;
    std::wstring m_candidateLocale;
    double m_candidateSince = 0.0;
    bool m_started = false;
    double m_lastLocaleProbe = -1.0;
};

#define DAWNACCESS_API __declspec(dllexport)
extern "C"
{
    DAWNACCESS_API CppUserModBase* start_mod()
    {
        return new DawnAccessMod();
    }

    DAWNACCESS_API void uninstall_mod(CppUserModBase* mod)
    {
        delete mod;
    }
}
