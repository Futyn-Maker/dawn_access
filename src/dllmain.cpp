// Dawn Access — screen-reader accessibility mod for Until Dawn (UE4SS C++ mod).
// Entry point: UE4SS loads Mods\DawnAccess\dlls\main.dll and calls start_mod().

#include "core/Config.hpp"
#include "core/GameThread.hpp"
#include "core/Log.hpp"
#include "core/ObjectUtil.hpp"
#include "core/Strings.hpp"
#include "diag/Diagnostics.hpp"
#include "features/Feature.hpp"
#include "features/Menus.hpp"
#include "hooks/HookDispatcher.hpp"
#include "hotkeys/Hotkeys.hpp"
#include "input/InputNames.hpp"
#include "locale/GameText.hpp"
#include "locale/Locale.hpp"
#include "speech/Outputs.hpp"
#include "speech/Sounds.hpp"
#include "speech/Speech.hpp"
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

        da::log::Info(L"DawnAccess {} starting; mod dir {}", ModVersion, m_modDir);
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

        // English strings first; the game's language is applied once the engine reports it.
        const std::wstring forcedLanguage = da::str::EqualsNoCase(settings.language, L"auto") ? L"" : settings.language;
        da::locale::LoadTables(m_modDir + L"\\lang", forcedLanguage.empty() ? L"en_US" : forcedLanguage);

        da::gamethread::Install();
        // What the player presses is watched so that only their own actions cut an
        // utterance short.
        da::input::InstallActivityTracker();
        da::hooks::Install();
        da::watch::Install();
        da::hotkeys::Install();
        da::diag::InstallCommandFile();
        da::diag::InstallFrameTimer();
        // The features are registered now and installed once the greeting has been said, so
        // that nothing of the screen is read before it.
        da::features::Register(std::make_unique<da::features::MenusFeature>());

        Unreal::Hook::FCallbackOptions options{};
        options.bReadonly = true;
        options.OwnerModName = STR("DawnAccess");
        options.HookName = STR("LoadMap");
        Unreal::Hook::RegisterLoadMapPostCallback(
            [](Unreal::Hook::TCallbackIterationData<bool>&, Unreal::UEngine*, Unreal::FWorldContext&, Unreal::FURL url, Unreal::UPendingNetGame*,
               Unreal::FString&)
            {
                (void)url;
                da::gamethread::Post(
                    []()
                    {
                        da::log::Info(L"map loaded; resetting caches");
                        da::obj::ResetCaches();
                        da::watch::Reset();
                        da::features::ResetMenus();
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

        if (da::cfg::Get().speakOnLoad)
        {
            const std::wstring output = da::speech::OutputName();
            const std::wstring reader = output.empty() ? da::locale::Mod(L"greeting.noreader") : output;
            // What the mod runs with belongs in the log either way; briefly the player is
            // told only that it is there.
            da::speech::Announce(da::cfg::Detailed() ? da::locale::Mod(L"greeting", std::vector<std::wstring>{ModVersion, reader, da::locale::CurrentCode()})
                                                     : da::locale::Mod(L"greeting.brief"));
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
