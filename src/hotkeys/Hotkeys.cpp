#include "hotkeys/Hotkeys.hpp"

#include "core/Config.hpp"
#include "core/GameThread.hpp"
#include "core/Gate.hpp"
#include "core/Log.hpp"
#include "core/ObjectUtil.hpp"
#include "core/Strings.hpp"
#include "core/Symbols.hpp"
#include "diag/Diagnostics.hpp"
#include "hotkeys/KeyHook.hpp"
#include "hotkeys/KeySpec.hpp"
#include "input/InputNames.hpp"
#include "locale/Locale.hpp"
#include "speech/Speech.hpp"
#include "watch/Watchers.hpp"

#include <windows.h>

#include <atomic>
#include <format>
#include <map>
#include <vector>

namespace da::hotkeys
{
    using RC::Unreal::UObject;

    namespace
    {
        struct KeyBinding
        {
            Command command;
            KeySpec key;
            bool wasDown = false;
        };

        std::vector<KeyBinding> g_keys;
        std::vector<Context> g_contexts;
        std::vector<Context> g_demoContexts;
        std::map<Command, std::vector<Handler>> g_handlers;

        bool InContext()
        {
            for (const Context inContext : g_contexts)
            {
                if (inContext()) return true;
            }
            return false;
        }

        bool InDemoContext()
        {
            for (const Context inContext : g_demoContexts)
            {
                if (inContext()) return true;
            }
            return false;
        }

        // Whether a command's scope lets it answer now.
        bool ScopeAnswers(Scope scope, bool context, bool demo)
        {
            if (scope == Scope::Context) return context;
            if (scope == Scope::Demo) return demo;
            return true;
        }

        // Whether a command answers now: every one while the mod is on, its switch alone
        // while it is off.
        bool Answers(Command command)
        {
            return gate::IsOn() || command == Command::ModOnOff;
        }

        void Bind(const Hotkey& hotkey)
        {
            const std::wstring spec = KeyOf(hotkey.command);
            KeyBinding binding{hotkey.command};
            if (!ParseKeySpec(spec, binding.key))
            {
                if (!spec.empty()) log::Error(L"hotkey '{}' for {} not understood", spec, hotkey.name);
                return;
            }
            g_keys.push_back(binding);
            log::Info(L"hotkey {} = {}", hotkey.name, spec);
        }

        bool GameWindowInForeground()
        {
            HWND fg = GetForegroundWindow();
            if (!fg) return false;
            DWORD pid = 0;
            GetWindowThreadProcessId(fg, &pid);
            return pid == GetCurrentProcessId();
        }

        // The window in front, for the log: its title and the program it belongs to.
        std::wstring ForegroundWindowName()
        {
            HWND fg = GetForegroundWindow();
            if (!fg) return L"no window";
            wchar_t title[256] = {};
            GetWindowTextW(fg, title, 255);
            DWORD pid = 0;
            GetWindowThreadProcessId(fg, &pid);
            std::wstring program = L"?";
            if (HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid))
            {
                wchar_t path[MAX_PATH] = {};
                DWORD size = MAX_PATH;
                if (QueryFullProcessImageNameW(process, 0, path, &size)) program = path;
                CloseHandle(process);
            }
            return std::format(L"\"{}\" of {} (process {})", title, program, pid);
        }

        // The pad's keys answer only while the game's window is in front and the pad is read: when
        // either goes, the log says so. At chapter one the scanner's keys went silent half a minute
        // before the game went down (2026-11-01), with no word of why.
        void NotePadState(bool inFront, bool valid)
        {
            static bool knownInFront = true;
            static bool knownValid = true;
            if (inFront != knownInFront)
            {
                knownInFront = inFront;
                if (inFront)
                    log::Info(L"hotkeys: the game's window is in front again; the pad's keys answer");
                else
                    log::Info(L"hotkeys: the game's window is no longer in front ({} is); the pad's keys are not answered", ForegroundWindowName());
            }
            if (inFront && valid != knownValid)
            {
                knownValid = valid;
                log::Info(L"hotkeys: {}", valid ? L"the pad is read again" : L"no pad reading (none connected, or the game reads none)");
            }
        }

        bool Down(int vk)
        {
            return (GetAsyncKeyState(vk) & 0x8000) != 0;
        }

        // The keys that stop speech the moment they go down (HotkeyTable's stop keys), by
        // virtual-key code with their spelling for the log, and whether each stops only while a
        // menu page is up.
        struct WatchedStop
        {
            int vk;
            std::wstring name;
            bool menuOnly;
        };
        std::vector<WatchedStop> g_keyboardStops;
        std::atomic<bool> g_menuUp{false}; // a menu page is up, as the game thread last saw it

        void StopForKeyboardKey(int vk)
        {
            for (const auto& stop : g_keyboardStops)
            {
                if (stop.vk != vk) continue;
                if (stop.menuOnly && !g_menuUp.load(std::memory_order_relaxed)) return;
                speech::StopForKey(stop.name);
                return;
            }
        }

        void StopForPadKey(std::wstring_view key)
        {
            for (const auto& stop : PadStopKeys())
            {
                if (key != stop.key) continue;
                if (stop.menuOnly && !g_menuUp.load(std::memory_order_relaxed)) return;
                speech::StopForKey(key);
                return;
            }
        }

        // The row of the game's controls that waits for a key to bind (the flag its click sets:
        // UBatesKeyRebindSettingWidget.bInSelectingKeyState), found where the focus stands and
        // kept while it waits. Every key is the game's meanwhile (hotkeys::TakeOf): the mod took
        // F1 to F9 before the game could see them, so that none of them could be bound.
        obj::ObjectKey g_waitingRow;

        bool WaitingForKey(UObject* row)
        {
            const auto* waits = static_cast<const uint8_t*>(sym::MemberOf(row, L"UBatesKeyRebindSettingWidget.bInSelectingKeyState"));
            return waits && *waits != 0;
        }

        bool GameWaitsForKey()
        {
            if (g_waitingRow.object)
            {
                if (obj::IsSame(g_waitingRow) && WaitingForKey(g_waitingRow.object)) return true;
                log::Info(L"hotkeys: the game's controls have their key; the mod's keys are the mod's again");
                g_waitingRow = {};
                return false;
            }
            UObject* focused = watch::CurrentFocused();
            UObject* row = focused ? obj::NearestAncestorOfClass(focused, L"BatesKeyRebindSettingWidget", 8) : nullptr;
            if (!row || !WaitingForKey(row)) return false;
            g_waitingRow = obj::KeyOf(row);
            log::Info(L"hotkeys: {} waits for a key to bind: every key is the game's until it has one", obj::ObjectName(row));
            return true;
        }

        void PollKeyboard()
        {
            if (g_keys.empty()) return;
            const bool gameWaits = GameWaitsForKey();
            // The hook hands over the presses it took; the poll below is for when it could
            // not be set.
            if (keyhook::Active())
            {
                // The context letters are the game's, or a text field's, outside the
                // situations the features declare; the hook leaves them alone there. While
                // the mod is off the hook takes nothing but the switch.
                keyhook::SetOn(gate::IsOn());
                keyhook::SetContext(gate::IsOn() && InContext());
                keyhook::SetDemo(gate::IsOn() && InDemoContext());
                keyhook::SetGameWaitsForKey(gameWaits);
                for (const int id : keyhook::TakePresses())
                {
                    if (id >= 0 && static_cast<size_t>(id) < g_keys.size()) Run(g_keys[static_cast<size_t>(id)].command, Source::Keyboard);
                }
                return;
            }
            if (!GameWindowInForeground())
            {
                for (auto& k : g_keys)
                    k.wasDown = false;
                return;
            }
            const bool ctrl = Down(VK_CONTROL);
            const bool alt = Down(VK_MENU);
            const bool shift = Down(VK_SHIFT);
            const bool context = InContext();
            const bool demo = InDemoContext();
            for (auto& k : g_keys)
            {
                const bool down = Down(k.key.vk);
                if (down && !k.wasDown && !gameWaits && ctrl == k.key.ctrl && alt == k.key.alt && shift == k.key.shift && Answers(k.command) &&
                    ScopeAnswers(Of(k.command).scope, context, demo))
                {
                    Run(k.command, Source::Keyboard);
                }
                k.wasDown = down;
            }
        }

        // ---- gamepad chords ----
        struct PadBinding
        {
            std::wstring keyName;
            Command command;
            bool wasDown = false;
        };
        // Chords: the hold button and one of these.
        std::vector<PadBinding> g_padBindings;
        // Buttons that need no chord because the game leaves them unused in the situations
        // the features declare; they answer only there.
        std::vector<PadBinding> g_exploreBindings;
        std::vector<PadBinding> g_demoBindings; // the same for the settings rows with an example
        std::wstring g_padHold;

        void PollBindings(const input::PadReading& pad, std::vector<PadBinding>& bindings, bool live)
        {
            for (auto& b : bindings)
            {
                if (!live)
                {
                    b.wasDown = false;
                    continue;
                }
                const bool down = input::PadKeyDown(pad, b.keyName);
                if (down && !b.wasDown && Answers(b.command)) Run(b.command, Source::Gamepad);
                b.wasDown = down;
            }
        }

        // The pad is read the way the game reads it, so the chords answer in a menu, where
        // the player controller no longer reports keys, as well as in play.
        void PollPad()
        {
            if ((g_padBindings.empty() && g_exploreBindings.empty() && g_demoBindings.empty()) || gamethread::FrameCount() % 3 != 0) return;
            const bool inFront = GameWindowInForeground();
            const input::PadReading pad = inFront ? input::ReadPad() : input::PadReading{};
            NotePadState(inFront, pad.valid);
            const bool chord = pad.valid && !g_padHold.empty() && input::PadKeyDown(pad, g_padHold);
            PollBindings(pad, g_padBindings, pad.valid && chord);
            PollBindings(pad, g_exploreBindings, pad.valid && !chord && gate::IsOn() && InContext());
            PollBindings(pad, g_demoBindings, pad.valid && !chord && gate::IsOn() && InDemoContext());
        }

        // The mod switched off and on. Off is said before the switch turns, so that it is
        // heard; on is followed by the screen as F6 reads it, since whatever came up while
        // the mod was off went unsaid.
        void ToggleMod()
        {
            if (gate::IsOn())
            {
                speech::Now(locale::Mod(L"onoff.off"));
                gate::Set(false);
                return;
            }
            gate::Set(true);
            speech::Now(locale::Mod(L"onoff.on"));
            const auto readout = diag::Readout();
            if (!readout.empty()) speech::Announce(readout);
        }
    }

    void Uninstall()
    {
        keyhook::Uninstall();
    }

    void AddContext(Context inContext)
    {
        if (inContext) g_contexts.push_back(inContext);
    }

    void AddDemoContext(Context inContext)
    {
        if (inContext) g_demoContexts.push_back(inContext);
    }

    void OnCommand(Command command, Handler handler)
    {
        if (handler) g_handlers[command].push_back(handler);
    }

    std::wstring KeyOf(Command command)
    {
        const Hotkey& hotkey = Of(command);
        return str::Trim(cfg::Get().Hotkey(hotkey.name, hotkey.key));
    }

    std::wstring ChordOf(Command command)
    {
        const Hotkey& hotkey = Of(command);
        if (std::wstring_view(hotkey.chordName).empty()) return {};
        return str::Trim(cfg::Get().Hotkey(hotkey.chordName, hotkey.chord));
    }

    std::wstring PadOf(Command command)
    {
        const Hotkey& hotkey = Of(command);
        if (std::wstring_view(hotkey.padName).empty()) return {};
        return str::Trim(cfg::Get().Hotkey(hotkey.padName, hotkey.pad));
    }

    std::wstring ChordHold()
    {
        return str::Trim(cfg::Get().Hotkey(kChordHoldSetting, kChordHoldDefault));
    }

    bool ChordsInHand()
    {
        return input::CurrentScheme() == input::Scheme::Gamepad && !ChordHold().empty();
    }

    std::wstring SpokenKey(Command command)
    {
        if (input::CurrentScheme() == input::Scheme::Gamepad)
        {
            const auto pad = PadOf(command);
            if (!pad.empty()) return input::KeyDisplayName(pad);
        }
        return KeyOf(command);
    }

    void Install()
    {
        g_keys.clear();
        for (const Hotkey& hotkey : Table())
            Bind(hotkey);

        std::vector<keyhook::Binding> bindings;
        for (size_t i = 0; i < g_keys.size(); ++i)
        {
            const Scope scope = Of(g_keys[i].command).scope;
            bindings.push_back(keyhook::Binding{g_keys[i].key.vk, g_keys[i].key.ctrl, g_keys[i].key.alt, g_keys[i].key.shift, static_cast<int>(i),
                                                scope == Scope::Context, scope == Scope::Switch, scope == Scope::Demo});
        }
        // The keys that stop what is being said (HotkeyTable's stop keys) are answered by the
        // hook: it runs before the game is given the key, so the stop always lands before
        // whatever the press brings up, while a polled key could arrive after the new screen
        // had been announced and silence that instead. Every other key only lets the next
        // answer interrupt (speech::Focus).
        g_keyboardStops.clear();
        std::vector<int> stopVks;
        for (const auto& stop : KeyboardStopKeys())
        {
            KeySpec key;
            if (!ParseKeySpec(stop.key, key)) continue;
            g_keyboardStops.push_back({key.vk, stop.key, stop.menuOnly});
            stopVks.push_back(key.vk);
        }
        keyhook::Watch(stopVks, &StopForKeyboardKey);
        if (keyhook::Install(bindings))
            log::Info(L"keyboard hook installed: the mod's keys are taken ahead of the screen reader and the game");
        else
            log::Error(L"the keyboard hook could not be set; the mod's keys are polled, and a key the screen reader takes for itself is lost");

        g_padHold = ChordHold();
        input::SetChordHold(g_padHold);
        // The pad's own stop keys, answered where the game reads its pad, which is before it
        // acts on them. A button pressed under the chord hold is hidden from that reading and
        // so stops nothing: the chord's own answer is said.
        std::vector<std::wstring> stopButtons;
        for (const auto& stop : PadStopKeys())
            stopButtons.emplace_back(stop.key);
        input::WatchPadButtons(stopButtons, &StopForPadKey);
        g_padBindings.clear();
        if (!g_padHold.empty())
        {
            for (const Hotkey& hotkey : Table())
            {
                const auto button = ChordOf(hotkey.command);
                if (button.empty()) continue;
                g_padBindings.push_back({button, hotkey.command});
                log::Info(L"gamepad {} + {} = {}", g_padHold, button, hotkey.name);
            }
        }
        g_exploreBindings.clear();
        g_demoBindings.clear();
        for (const Hotkey& hotkey : Table())
        {
            const auto button = PadOf(hotkey.command);
            if (button.empty()) continue;
            (hotkey.scope == Scope::Demo ? g_demoBindings : g_exploreBindings).push_back({button, hotkey.command});
            log::Info(L"gamepad in context: {} = {}", button, hotkey.name);
        }
        gamethread::AddPoller(L"hotkeys",
                              [](float)
                              {
                                  PollKeyboard();
                                  PollPad();
                                  g_menuUp.store(watch::CurrentScreen() != nullptr, std::memory_order_relaxed);
                              });
    }

    namespace
    {
        void RunImpl(Command command);

        void RunGuarded(void* context)
        {
            RunImpl(*static_cast<Command*>(context));
        }
    }

    void Run(Command command, Source source)
    {
        log::Info(L"hotkey: {}{}", CommandName(command), source == Source::Keyboard ? L" (keyboard)" : (source == Source::Gamepad ? L" (gamepad)" : L""));
        // The answer is worded for the device the command came from.
        if (source == Source::Keyboard) input::NoteHotkeyDevice(input::Scheme::MouseKeyboard);
        if (source == Source::Gamepad) input::NoteHotkeyDevice(input::Scheme::Gamepad);
        // A hotkey must never take the game down, nor the presses after it: a fault or a C++
        // exception abandons the command and is logged.
        try
        {
            if (!obj::SafeInvoke(&RunGuarded, &command)) log::Error(L"hotkey {} ran into a memory fault and was abandoned", CommandName(command));
        }
        catch (const std::exception& e)
        {
            log::Error(L"hotkey {} failed: {}", CommandName(command), str::Utf8ToWide(e.what()));
        }
        catch (...)
        {
            log::Error(L"hotkey {} failed (unknown exception)", CommandName(command));
        }
    }

    namespace
    {
        // The features' commands, each given to the handlers registered for it in turn.
        void Dispatch(Command command)
        {
            const auto it = g_handlers.find(command);
            if (it == g_handlers.end()) return;
            for (const Handler handler : it->second)
            {
                if (handler()) return;
            }
        }

        void RunImpl(Command command)
        {
            switch (command)
            {
            case Command::Repeat: speech::Repeat(); break;
            case Command::ReadScreen: speech::Now(diag::ReadScreen()); break;
            case Command::Stop: speech::Stop(); break;
            case Command::Help: speech::Now(diag::Help()); break;
            case Command::Speech: speech::ToggleOutput(); break;
            case Command::Verbosity: speech::Now(locale::Mod(cfg::ToggleVerbosity() ? L"verbosity.full" : L"verbosity.brief")); break;
            case Command::ModOnOff: ToggleMod(); break;
            case Command::DevDumpTree:
                diag::DumpScreen();
                speech::Announce(locale::Mod(L"diag.dumped"));
                break;
            case Command::DevTrace: diag::ToggleTrace(); break;
            case Command::DevLogLevel: diag::CycleLogLevel(); break;
            default: Dispatch(command); break;
            }
        }
    }
}
