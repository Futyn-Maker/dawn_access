#include "diag/Diagnostics.hpp"

#include "core/Config.hpp"
#include "core/Freed.hpp"
#include "core/GameThread.hpp"
#include "core/Log.hpp"
#include "core/ObjectUtil.hpp"
#include "core/Strings.hpp"
#include "features/Feature.hpp"
#include "hooks/HookDispatcher.hpp"
#include "hotkeys/Hotkeys.hpp"
#include "input/InputNames.hpp"
#include "locale/Locale.hpp"
#include "speech/PrismBridge.hpp"
#include "speech/Speech.hpp"
#include "watch/Watchers.hpp"

#include <UE4SSProgram.hpp>

#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>

namespace da::diag
{
    using RC::Unreal::UObject;

    namespace
    {
        std::wstring g_modDir;
        std::map<std::wstring, CommandHandler> g_commands;

        std::wstring TimestampForFile()
        {
            SYSTEMTIME t{};
            GetLocalTime(&t);
            return std::format(L"{:04}{:02}{:02}-{:02}{:02}{:02}", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
        }

        std::wstring DumpsDir()
        {
            if (g_modDir.empty()) return {};
            std::error_code ec;
            std::filesystem::create_directories(std::filesystem::path(g_modDir) / L"dumps", ec);
            return g_modDir + L"\\dumps";
        }
    }

    void SetModDir(const std::wstring& modDir)
    {
        g_modDir = modDir;
    }

    void RegisterCommand(std::wstring_view name, CommandHandler handler)
    {
        if (handler) g_commands[str::ToLower(name)] = handler;
    }

    // Every property of one object, by its full path or the name of its class (the first
    // live instance then), so that a default value the game keeps in its code can be read.
    void DumpProperties(const std::wstring& what)
    {
        UObject* object = obj::FindObject(what);
        if (!object) object = obj::FindFirstLive(what);
        if (!object)
        {
            log::Error(L"props: nothing is called {}", what);
            return;
        }
        log::Info(L"props: {}", obj::FullName(object));
        auto* cls = object->GetClassPrivate();
        if (!cls) return;
        for (auto* prop : cls->ForEachPropertyInChain())
        {
            if (!prop) continue;
            log::Info(L"props:   {} ({}) = {}", prop->GetName(), obj::PropertyTypeName(prop), obj::ValueToString(prop, obj::ValuePtr(object, prop)));
        }
    }

    namespace
    {
        // Every object the engine holds, with its properties, as UE4SS's own dumper writes it.
        void DumpObjects()
        {
            const std::wstring dir = DumpsDir();
            if (dir.empty()) return;
            const std::wstring path = dir + L"\\objects-" + TimestampForFile() + L".txt";
            log::Info(L"objects: writing {}", path);
            RC::UE4SSProgram::dump_all_objects_and_properties(path);
            log::Info(L"objects: written");
        }

        // C++ headers of every class and struct the engine holds, as UE4SS's generator
        // writes them, in a folder of the dumps folder.
        void DumpHeaders()
        {
            const std::wstring dir = DumpsDir();
            if (dir.empty()) return;
            const std::wstring path = dir + L"\\headers-" + TimestampForFile();
            log::Info(L"headers: writing {}", path);
            RC::UE4SSProgram::get_program().generate_cxx_headers(std::filesystem::path(path));
            log::Info(L"headers: written");
        }

        // Every focus signal of the UI at this moment, side by side: the game's own record in
        // its navigation trees, the engine's focus and hover on the game's buttons, and the
        // actions its input router has bound.
        void ProbeFocus()
        {
            for (auto* system : obj::FindAllLive(L"BMUIPageReflectionSystem"))
            {
                UObject* tree = nullptr;
                obj::ReadObject(system, L"ActiveTree", tree);
                log::Info(L"probe: reflection system {} active tree {}", obj::ObjectName(system), obj::ObjectName(tree));
            }
            for (auto* tree : obj::FindAllLive(L"BMUITree"))
            {
                UObject* container = nullptr;
                UObject* leaf = nullptr;
                UObject* page = nullptr;
                UObject* cached = nullptr;
                obj::ReadObject(tree, L"RootContainer", container);
                if (obj::ReadObject(tree, L"LeafNode", leaf) && leaf && obj::IsLive(leaf))
                {
                    obj::ReadObject(leaf, L"PageObject", page);
                    obj::ReadObject(leaf, L"CachedFocusWidget", cached);
                }
                log::Info(L"probe: tree {} in {} leaf page {} {} focus {} {}", obj::ObjectName(tree), obj::ObjectName(container), obj::ClassName(page),
                          obj::ObjectName(page), obj::ClassName(cached), obj::ObjectName(cached));
            }
            for (auto* button : obj::FindAllLive(L"BMUIButton"))
            {
                if (!obj::IsWidgetShown(button)) continue;
                bool enabled = true;
                obj::ReadBool(button, L"bIsButtonEnabled", enabled);
                UObject* inner = nullptr;
                obj::ReadObject(button, L"InternalButton", inner);
                log::Info(
                    L"probe: button {} {} \"{}\" focusedDescendants={} keyboardFocus={} anyUserFocus={} hovered={} innerFocus={} innerHovered={} enabled={}",
                    obj::ClassName(button), obj::ObjectName(button), str::Join(obj::DescendantTexts(button, 10), L" | "),
                    obj::CallForBool(button, L"HasFocusedDescendants"), obj::CallForBool(button, L"HasKeyboardFocus"),
                    obj::CallForBool(button, L"HasAnyUserFocus"), obj::CallForBool(button, L"IsHovered"),
                    inner && obj::IsLive(inner) ? obj::CallForBool(inner, L"HasKeyboardFocus") : false,
                    inner && obj::IsLive(inner) ? obj::CallForBool(inner, L"IsHovered") : false, enabled);
            }
            for (auto* router : obj::FindAllLive(L"BMUIInputRouter"))
            {
                std::vector<UObject*> bindings;
                obj::ReadObjectArray(router, L"ActiveUIBindings", bindings);
                std::vector<std::wstring> names;
                for (auto* binding : bindings)
                {
                    UObject* action = nullptr;
                    if (binding && obj::IsLive(binding) && obj::ReadObject(binding, L"InputAction", action) && action) names.push_back(obj::ObjectName(action));
                }
                log::Info(L"probe: router {} active bindings [{}]", obj::ObjectName(router), str::Join(names, L", "));
            }
        }

        void RunCommandImpl(const std::wstring& line)
        {
            const std::wstring cmd = str::Trim(line);
            if (cmd.empty()) return;
            log::Info(L"command: {}", cmd);
            const auto lower = str::ToLower(cmd);
            if (lower == L"trace on")
                hooks::SetTrace(true);
            else if (lower == L"trace off")
                hooks::SetTrace(false);
            else if (lower == L"dump")
                DumpScreen();
            else if (lower == L"read")
                speech::Now(ReadScreen());
            else if (lower == L"help")
                speech::Now(Help());
            else if (lower == L"onoff")
                hotkeys::Run(hotkeys::Command::ModOnOff);
            else if (str::StartsWith(lower, L"loglevel "))
            {
                log::Level level;
                if (log::ParseLevel(cmd.substr(9), level)) SetLogLevel(level);
            }
            else if (str::StartsWith(lower, L"say "))
                speech::Announce(cmd.substr(4));
            else if (str::StartsWith(lower, L"note "))
                log::Info(L"note: {}", str::Trim(cmd.substr(5)));
            else if (lower == L"walkkeys")
                log::Info(L"input: {}", input::DescribeWalkKeys());
            else if (str::StartsWith(lower, L"key "))
                log::Info(L"{}", input::DescribeAction(str::Trim(cmd.substr(4))));
            else if (str::StartsWith(lower, L"props "))
                DumpProperties(str::Trim(cmd.substr(6)));
            else if (lower == L"objects")
                DumpObjects();
            else if (lower == L"headers")
                DumpHeaders();
            else if (lower == L"focus")
                ProbeFocus();
            else
            {
                const size_t space = lower.find(L' ');
                const std::wstring name = lower.substr(0, space);
                const auto it = g_commands.find(name);
                if (it == g_commands.end())
                    log::Error(L"unknown command: {}", cmd);
                else
                    it->second(space == std::wstring::npos ? std::wstring() : str::Trim(cmd.substr(space + 1)));
            }
        }

        void RunCommandGuarded(void* context)
        {
            RunCommandImpl(*static_cast<const std::wstring*>(context));
        }

        // A command must never take the game down, nor the commands after it in the file: a
        // fault or a C++ exception abandons it and is logged.
        void RunCommand(const std::wstring& line)
        {
            try
            {
                if (!obj::SafeInvoke(&RunCommandGuarded, const_cast<std::wstring*>(&line)))
                    log::Error(L"command ran into a memory fault and was abandoned: {}", line);
            }
            catch (const std::exception& e)
            {
                log::Error(L"command failed: {}: {}", line, str::Utf8ToWide(e.what()));
            }
            catch (...)
            {
                log::Error(L"command failed (unknown exception): {}", line);
            }
        }

        void PollCommandFile(float)
        {
            static double lastCheck = -1.0;
            const double now = gamethread::NowSeconds();
            if (now - lastCheck < 0.5) return;
            lastCheck = now;
            if (g_modDir.empty()) return;
            const std::filesystem::path path = std::filesystem::path(g_modDir) / L"command.txt";
            std::error_code ec;
            if (!std::filesystem::exists(path, ec)) return;
            std::string bytes;
            {
                std::ifstream raw(path, std::ios::binary);
                bytes.assign((std::istreambuf_iterator<char>(raw)), std::istreambuf_iterator<char>());
            }
            std::filesystem::remove(path, ec);
            // An editor that marks the file as UTF-8 puts a mark before the first command.
            if (bytes.starts_with("\xEF\xBB\xBF")) bytes.erase(0, 3);
            for (const auto& line : str::Split(str::Utf8ToWide(bytes), L'\n'))
            {
                RunCommand(line);
            }
        }

        // ---- frame times ----
        std::vector<float> g_frames;
        double g_framesSince = -1.0;

        void PollFrameTimes(float deltaSeconds)
        {
            const double now = gamethread::NowSeconds();
            if (g_framesSince < 0.0) g_framesSince = now;
            if (deltaSeconds > 0.0f) g_frames.push_back(deltaSeconds * 1000.0f);
            if (now - g_framesSince < 30.0 || g_frames.empty()) return;
            std::sort(g_frames.begin(), g_frames.end());
            const auto at = [&](double fraction)
            { return g_frames[std::min(g_frames.size() - 1, static_cast<size_t>(fraction * static_cast<double>(g_frames.size())))]; };
            // The mod's own share of those frames: what its pollers took, and which took longest.
            auto costs = gamethread::TakePollerCosts();
            std::wstring share;
            if (!costs.frameMs.empty())
            {
                std::sort(costs.frameMs.begin(), costs.frameMs.end());
                const auto cost = [&](double fraction)
                { return costs.frameMs[std::min(costs.frameMs.size() - 1, static_cast<size_t>(fraction * static_cast<double>(costs.frameMs.size())))]; };
                share = std::format(L"; mod median {:.2f} ms, 95% {:.2f} ms, worst {:.1f} ms", cost(0.5), cost(0.95), costs.frameMs.back());
                const auto worst = std::max_element(costs.worst.begin(), costs.worst.end(), [](const auto& a, const auto& b) { return a.second < b.second; });
                if (worst != costs.worst.end()) share += std::format(L" ({} {:.1f} ms)", worst->first, worst->second);
            }
            // How many of the mod's lookups by address the game's frees have taken back (core/Freed),
            // when that has grown: the evidence that classes come and go under the mod.
            static unsigned long long forgottenSaid = 0;
            const unsigned long long forgotten = freed::ForgottenCount();
            if (forgotten != forgottenSaid)
            {
                share += std::format(L"; {} lookup(s) forgotten as the game freed their objects", forgotten);
                forgottenSaid = forgotten;
            }
            log::Info(L"frames: {} in {:.0f} s; median {:.1f} ms, 95% {:.1f} ms, 99% {:.1f} ms, worst {:.1f} ms{}", g_frames.size(), now - g_framesSince,
                      at(0.5), at(0.95), at(0.99), g_frames.back(), share);
            g_frames.clear();
            g_framesSince = now;
        }
    }

    void InstallCommandFile()
    {
        gamethread::AddPoller(L"commandfile", &PollCommandFile);
    }

    void InstallFrameTimer()
    {
        gamethread::AddPoller(L"frames", &PollFrameTimes);
    }

    namespace
    {
        // The flags the game keeps of its first launch, in its user settings object; each is
        // cleared once, the first time it is seen set, within the first two minutes.
        void PollFirstLaunch(float)
        {
            static bool done = !cfg::Get().firstLaunch;
            static double since = -1.0;
            static std::set<std::wstring> cleared;
            if (done) return;
            const double now = gamethread::NowSeconds();
            if (since < 0.0) since = now;
            if (now - since > 120.0)
            {
                done = true;
                return;
            }
            UObject* settings = obj::FindFirstLive(L"BatesGameUserSettings");
            if (!settings) return;
            for (const wchar_t* name :
                 {L"bHasCompletedFirstTimeSplash", L"bHasCompletedFirstTimeEULA", L"bHasCompletedFirstTimeSettings", L"bPCAllowDataCollectionChoiceMade"})
            {
                if (cleared.contains(name)) continue;
                bool set = false;
                if (!obj::ReadBool(settings, name, set))
                {
                    cleared.insert(name);
                    log::Info(L"diag: the game keeps no flag {}", name);
                    continue;
                }
                if (!set) continue;
                cleared.insert(name);
                log::Info(L"diag: first-launch flag {} cleared {}", name, obj::WriteBool(settings, name, false) ? L"" : L"(write failed)");
            }
            if (cleared.size() >= 4) done = true;
        }
    }

    void InstallFirstLaunch()
    {
        if (cfg::Get().firstLaunch) log::Info(L"diag: the first-launch screens are asked for again ([Diag] FirstLaunch=1)");
        gamethread::AddPoller(L"firstlaunch", &PollFirstLaunch);
    }

    // How a widget hangs in the drawn hierarchy: through a slot, as the root of its owner's
    // tree, in the viewport, or not at all; and a render transform that moves or scales it.
    std::wstring Attachment(UObject* w)
    {
        std::wstring out;
        UObject* slot = nullptr;
        bool attached = obj::ReadObject(w, L"Slot", slot) && slot;
        if (!attached)
        {
            UObject* outer = w->GetOuterPrivate();
            UObject* root = nullptr;
            if (outer && obj::IsA(outer, L"WidgetTree") && obj::ReadObject(outer, L"RootWidget", root) && root == w) attached = true;
        }
        if (!attached)
        {
            if (obj::IsA(w, L"UserWidget"))
                out += obj::CallForBool(w, L"IsInViewport") ? L" viewport=yes" : L" viewport=no";
            else
                out += L" detached";
        }
        // FWidgetTransform: the translation and the scale first, two doubles each.
        if (const auto* t = static_cast<const double*>(obj::StructPtr(w, L"RenderTransform")))
        {
            if (t[0] != 0.0 || t[1] != 0.0) out += std::format(L" offset={:.0f},{:.0f}", t[0], t[1]);
            if (t[2] != 1.0 || t[3] != 1.0) out += std::format(L" scale={:.2f},{:.2f}", t[2], t[3]);
        }
        return out;
    }

    // The state a control carries besides its text: whether it is enabled, checked, which entry
    // of its carousel it shows, where its slider stands.
    std::wstring ControlState(UObject* w)
    {
        std::wstring out;
        bool enabled = true;
        if (obj::ReadBool(w, L"bIsButtonEnabled", enabled) && !enabled) out += L" disabled";
        bool checked = false;
        if (obj::FindProperty(w, L"bIsChecked") && obj::ReadBool(w, L"bIsChecked", checked)) out += checked ? L" checked" : L" unchecked";
        int64_t index = 0;
        if (obj::FindProperty(w, L"CurrentIndex") && obj::ReadInt(w, L"CurrentIndex", index)) out += std::format(L" index={}", index);
        double value = 0.0;
        if (obj::IsA(w, L"Slider") && obj::ReadFloat(w, L"Value", value)) out += std::format(L" value={:.3f}", value);
        if (obj::IsA(w, L"CheckBox"))
        {
            int64_t state = 0;
            if (obj::ReadInt(w, L"CheckedState", state)) out += std::format(L" state={}", state);
        }
        if (obj::IsA(w, L"ProgressBar") && obj::ReadFloat(w, L"Percent", value)) out += std::format(L" percent={:.3f}", value);
        // The picture behind an image, and the text the game attached for its own narration.
        if (obj::IsA(w, L"Image"))
        {
            auto* brush = obj::FindProperty(w, L"Brush");
            UObject* resource = nullptr;
            if (brush && obj::ReadObjectAt(obj::ValuePtr(w, brush), obj::StructMember(brush, L"ResourceObject"), resource) && resource && obj::IsLive(resource))
            {
                out += L" image=" + obj::ObjectName(resource);
                // A material instance names its parents and the textures set as its parameters.
                for (int depth = 0; resource && obj::IsLive(resource) && obj::IsA(resource, L"MaterialInstance") && depth < 4; ++depth)
                {
                    obj::ForEachArrayElement(resource, obj::FindProperty(resource, L"TextureParameterValues"),
                                             [&](void* element, obj::FProperty* inner)
                                             {
                                                 UObject* texture = nullptr;
                                                 if (obj::ReadObjectAt(element, obj::StructMember(inner, L"ParameterValue"), texture) && texture &&
                                                     obj::IsLive(texture))
                                                     out += L" tex=" + obj::ObjectName(texture);
                                             });
                    UObject* parent = nullptr;
                    if (!obj::ReadObject(resource, L"Parent", parent) || !parent) break;
                    out += L" parent=" + obj::ObjectName(parent);
                    resource = parent;
                }
            }
        }
        UObject* a11y = nullptr;
        if (obj::ReadObject(w, L"AccessibleWidgetData", a11y) && a11y && obj::IsLive(a11y))
        {
            std::wstring text;
            obj::ReadString(a11y, L"AccessibleText", text);
            if (!text.empty()) out += L" a11y=\"" + str::StripMarkup(text) + L"\"";
        }
        return out;
    }

    std::wstring DumpsDirectory()
    {
        return DumpsDir();
    }

    std::wstring ModDirectory()
    {
        return g_modDir;
    }

    void DumpScreen()
    {
        std::vector<std::wstring> lines;
        lines.push_back(L"== DawnAccess screen dump ==");
        lines.push_back(L"last hook: " + hooks::LastHook());

        const auto widgets = obj::FindAllLive(L"UserWidget");
        size_t shown = 0;
        for (auto* widget : widgets)
        {
            if (!obj::IsWidgetShown(widget)) continue;
            // Only report top-level user widgets (not nested inside another user widget's tree).
            UObject* owner = obj::FindOuterUserWidget(widget);
            if (owner) continue;
            ++shown;
            lines.push_back(std::format(L"[widget] {} {}{}", obj::ClassName(widget), obj::ObjectName(widget), Attachment(widget)));
            // Deep enough to reach what the mod itself reads: a dump that stops short shows
            // no text where there is text, and a screen then looks as though it had none. The
            // game's setting rows keep their label some thirty layouts down.
            obj::WalkWidgetTree(widget, 48,
                                [&](UObject* w, int depth)
                                {
                                    // A widget that is not drawn hides everything below it with itself.
                                    if (!obj::IsWidgetVisible(w)) return false;
                                    std::wstring text = str::StripMarkup(obj::TextOf(w));
                                    std::wstring pad(static_cast<size_t>(depth) * 2, L' ');
                                    const std::wstring state = ControlState(w);
                                    if (!text.empty() || !state.empty() || obj::IsA(w, L"UserWidget") || obj::IsA(w, L"Image"))
                                    {
                                        const double opacity = obj::WidgetOpacity(w);
                                        lines.push_back(std::format(
                                            L"{}- {} {}{}{}{}", pad, obj::ClassName(w), obj::ObjectName(w), text.empty() ? L"" : L" = \"" + text + L"\"", state,
                                            (opacity < 0.999 ? std::format(L" opacity={:.2f}", opacity) : std::wstring()) + Attachment(w)));
                                    }
                                    return true;
                                });
        }
        lines.push_back(std::format(L"shown top-level user widgets: {} (of {} live)", shown, widgets.size()));
        for (const auto& [name, instance] : watch::LiveHudInstances())
        {
            lines.push_back(std::format(L"[shown] {} {} texts: {}", name, obj::ObjectName(instance), str::Join(obj::DescendantTexts(instance), L" | ")));
        }
        for (auto* page : watch::CurrentScreens())
        {
            lines.push_back(std::format(L"[page] {} {}", obj::ClassName(page), obj::ObjectName(page)));
        }
        lines.push_back(std::format(L"current screen: {}  focused: {} {}", obj::ClassName(watch::CurrentScreen()), obj::ClassName(watch::CurrentFocused()),
                                    obj::ObjectName(watch::CurrentFocused())));

        std::wstring path;
        const std::wstring dir = DumpsDir();
        if (!dir.empty())
        {
            path = dir + L"\\screen-" + TimestampForFile() + L".txt";
            std::ofstream out(path, std::ios::binary);
            if (out)
            {
                out << "\xEF\xBB\xBF";
                for (const auto& line : lines)
                    out << str::WideToUtf8(line) << "\n";
            }
        }
        for (const auto& line : lines)
            log::Info(L"dump: {}", line);
        log::Info(L"screen dump written to {}", path);
    }

    void ToggleTrace()
    {
        const bool on = !hooks::TraceEnabled();
        hooks::SetTrace(on);
        speech::Announce(locale::Mod(on ? L"diag.trace.on" : L"diag.trace.off"));
    }

    void SetLogLevel(log::Level level)
    {
        log::SetLevel(level);
        prism::FollowLogLevel();
        // Kept in the ini, so that the next session logs the way this one was left.
        if (!cfg::Persist(L"General", L"LogLevel", log::LevelName(level))) log::Error(L"diag: the log level could not be saved to DawnAccess.ini");
        log::Info(L"diag: log level {}", log::LevelName(level));
    }

    void CycleLogLevel()
    {
        SetLogLevel(log::CycleLevel());
        speech::Announce(locale::Mod(L"diag.loglevel", log::LevelName(log::GetLevel())));
    }

    namespace
    {
        // The texts of the top-level widgets on screen, after whatever the features said. The
        // container that holds the screen being read is left out: its texts are the screen's.
        void AddScreenTexts(std::vector<std::wstring>& parts)
        {
            UObject* screen = watch::CurrentScreen();
            for (const auto& [name, instance] : watch::LiveHudInstances())
            {
                if (screen && obj::IsDescendantOf(screen, instance)) continue;
                const auto texts = obj::DescendantTexts(instance, 6);
                if (!texts.empty()) parts.push_back(str::Join(texts, L", "));
            }
        }
    }

    std::wstring Readout()
    {
        std::vector<std::wstring> parts;
        features::DescribeAll(parts);
        AddScreenTexts(parts);
        return str::JoinSentences(parts);
    }

    std::wstring ReadScreen()
    {
        // Features describe the current screen (menus, HUD, ...). Only when none of them
        // has anything to say do we fall back to the raw focused-widget text.
        std::vector<std::wstring> parts;
        features::DescribeAll(parts);
        if (parts.empty())
        {
            if (UObject* focused = watch::CurrentFocused())
            {
                const auto texts = obj::DescendantTexts(focused, 12);
                std::wstring text = str::Join(texts, L", ");
                if (text.empty()) text = obj::ObjectName(focused);
                parts.push_back(locale::Mod(L"read.focused", text));
            }
        }
        AddScreenTexts(parts);
        if (parts.empty())
        {
            const auto last = speech::Last();
            if (!last.empty())
                parts.push_back(locale::Mod(L"read.last", last));
            else
                parts.push_back(locale::Mod(L"read.nothing"));
        }
        return str::JoinSentences(parts);
    }

    std::wstring Help()
    {
        using hotkeys::Command;
        std::vector<std::wstring> parts;
        parts.push_back(locale::Mod(L"help.intro"));
        // The keys of the device in the player's hands: the gamepad's chords after a gamepad
        // press, the keyboard's keys otherwise.
        const bool pad = hotkeys::ChordsInHand();
        const auto hold = input::KeyDisplayName(hotkeys::ChordHold());
        const auto chord = [](Command command) { return input::KeyDisplayName(hotkeys::ChordOf(command)); };
        if (pad)
        {
            parts.push_back(locale::Mod(L"help.chords", std::vector<std::wstring>{hold, chord(Command::Repeat), chord(Command::ReadScreen),
                                                                                  chord(Command::Stop), chord(Command::Help)}));
            parts.push_back(locale::Mod(L"help.speech.pad", hold, chord(Command::Speech)));
            parts.push_back(locale::Mod(L"help.verbosity.pad", hold, chord(Command::Verbosity)));
            parts.push_back(locale::Mod(L"help.onoff.pad", hold, chord(Command::ModOnOff)));
        }
        else
        {
            parts.push_back(locale::Mod(L"help.hotkeys", std::vector<std::wstring>{hotkeys::KeyOf(Command::Repeat), hotkeys::KeyOf(Command::ReadScreen),
                                                                                   hotkeys::KeyOf(Command::Stop), hotkeys::KeyOf(Command::Help)}));
            parts.push_back(locale::Mod(L"help.speech", hotkeys::KeyOf(Command::Speech)));
            parts.push_back(locale::Mod(L"help.verbosity", hotkeys::KeyOf(Command::Verbosity)));
            parts.push_back(locale::Mod(L"help.onoff", hotkeys::KeyOf(Command::ModOnOff)));
        }
        features::HelpAll(parts);
        // The developer's keys close the help, after everything a player uses.
        if (pad)
            parts.push_back(locale::Mod(L"help.diag.pad", std::vector<std::wstring>{hold, chord(Command::DevDumpTree), chord(Command::DevLogLevel)}));
        else
            parts.push_back(locale::Mod(L"help.diag", hotkeys::KeyOf(Command::DevDumpTree), hotkeys::KeyOf(Command::DevLogLevel)));
        return str::JoinSentences(parts);
    }
}
