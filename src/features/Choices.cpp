#include "features/Choices.hpp"

#include "features/ChoiceRules.hpp"
#include "features/Qte.hpp"

#include "core/Config.hpp"
#include "core/GameThread.hpp"
#include "core/Log.hpp"
#include "core/ObjectUtil.hpp"
#include "core/ParamReader.hpp"
#include "core/Strings.hpp"
#include "hooks/HookDispatcher.hpp"
#include "hotkeys/KeyHook.hpp"
#include "locale/Locale.hpp"
#include "speech/Sounds.hpp"
#include "speech/Speech.hpp"
#include "ui/Widgets.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace da::features
{
    using RC::Unreal::FFrame;
    using RC::Unreal::UObject;

    namespace
    {
        // The choices as the rules keep them (features/ChoiceRules), and the option taken, said as
        // the side is taken, cutting what is being said (choice::Chosen).
        using choice::Choice;
        using choice::Option;
        choice::Book g_book;
        choice::Chosen g_chosen;

        UObject* WidgetOf(const Option& option)
        {
            return static_cast<UObject*>(option.widget);
        }

        const wchar_t* Side(int64_t direction)
        {
            return direction == 0 ? L"dir.left" : L"dir.right";
        }

        // The key an option is taken with, as the game draws it beside the option.
        std::wstring KeyOf(const Option& option)
        {
            std::vector<std::wstring> keys;
            for (const auto& prompt : ui::PromptsIn(WidgetOf(option)))
            {
                if (!prompt.key.empty()) keys.push_back(prompt.key);
            }
            return str::Join(keys, L", ");
        }

        bool Timed(const Choice& choice)
        {
            return std::any_of(choice.options.begin(), choice.options.end(), [](const Option& o) { return o.timed; });
        }

        // The whole readout: the kind of choice when detailed (a sound already says it), then
        // each option with its side and key.
        std::wstring ChoiceText(const Choice& choice)
        {
            std::vector<std::wstring> parts;
            if (cfg::Detailed()) parts.push_back(locale::Mod(Timed(choice) ? L"choice.timed" : L"choice.heading"));
            for (const auto& option : choice.options)
            {
                if (!obj::IsLive(WidgetOf(option))) continue;
                const auto words = choice::Words(option);
                if (words.empty()) continue;
                std::wstring line = locale::Mod(Side(option.direction)) + L": " + words;
                // The share of players who took it, where the game shows it (choice::SharePercent).
                if (const auto share = choice::SharePercent(option.globalShare); !share.empty()) line += L", " + share;
                const auto key = KeyOf(option);
                if (!key.empty()) line += L", " + key;
                parts.push_back(line);
            }
            return str::JoinSentences(parts);
        }

        void Announce(Choice& choice)
        {
            choice.announced = true;
            const auto text = ChoiceText(choice);
            log::Info(L"choices: read \"{}\"", text);
            if (Timed(choice)) sounds::Play(sounds::Cue::Timer);
            if (!text.empty()) speech::Announce(text);
        }

        // The game set an option up with its words: a choice of its own, or the other side of
        // the one being set up (choice::Book::SetUp).
        void OnSetUp(UObject* self, FFrame& stack)
        {
            Option option;
            option.widget = self;
            params::Int(stack, L"InInputDirection", option.direction);
            params::String(stack, L"InText", option.text);
            params::String(stack, L"InSubText", option.subText);
            params::Bool(stack, L"bCanTimeOut", option.timed);
            option.text = str::StripMarkup(option.text);
            option.subText = str::StripMarkup(option.subText);
            const double now = gamethread::NowSeconds();
            log::Info(L"choices: option {} set up: {} \"{}\" \"{}\"{}", obj::ObjectName(self), option.direction == 0 ? L"left" : L"right", option.text,
                      option.subText, option.timed ? L", timed" : L"");
            g_book.SetUp(option, now);
        }

        // The player holds toward an option: its words are said as each hold begins
        // (choice::Hold). The ring the hold fills on the option stays silent: the words at the
        // start of the hold and the tone at the end are the whole of it, as a held key keeps
        // the reader silent in between anyway.
        void OnSelection(UObject* self, FFrame& stack)
        {
            double progress = 0.0;
            params::Float(stack, L"InProgress", progress);
            Option* option = nullptr;
            Choice* choice = g_book.Of(self, &option);
            if (!choice || !option || choice->finished) return;
            if (choice::Hold(*option, progress))
            {
                // With the players' share where the game shows it (choice::HeldWords).
                const auto words = choice::HeldWords(*option);
                log::Verbose(L"choices: holding toward \"{}\"", words);
                if (!words.empty()) speech::Focus(words);
            }
        }

        // The game shows, as the choice starts, the share of players who took each option, once the
        // game is finished and its setting "Глобальная статистика" is on (UBatesDirectionalChoiceStage::
        // OnShowGlobalStatsChanged): said with the option.
        void OnGlobalStats(UObject* self, FFrame& stack)
        {
            bool shown = false;
            double share = -1.0;
            params::Bool(stack, L"bInShowGlobalStats", shown);
            params::Float(stack, L"InPercentage", share);
            Option* option = nullptr;
            g_book.Of(self, &option);
            log::Info(L"choices: option {} shows the players' share {}: {:.3f}", obj::ObjectName(self), shown ? L"" : L"not", share);
            if (option) option->globalShare = shown ? share : -1.0;
        }

        void OnTimeout(UObject* self, FFrame& stack)
        {
            double progress = 0.0;
            params::Float(stack, L"InProgress", progress);
            if (Choice* choice = g_book.Of(self)) choice->timeout = progress;
        }

        // What the game's verdict means: the side taken, with its tone, cutting what is being said,
        // and at the detailed level its word in its place (choice::Chosen); the time run out; or an
        // end of the stage's own, without a word.
        void Finish(Choice& choice, int64_t result, const std::wstring& how)
        {
            std::wstring chosen;
            const choice::End end = g_book.Finish(choice, result, &chosen);
            if (end == choice::End::None) return;
            log::Info(L"choices: ended with {} ({})", result, how);
            if (end == choice::End::Left || end == choice::End::Right)
            {
                sounds::Play(sounds::Cue::Confirm);
                const std::wstring words = cfg::Detailed() && !chosen.empty() ? locale::Mod(L"choice.chosen", chosen) : std::wstring();
                switch (g_chosen.Take(words, gamethread::NowSeconds(), keyhook::KeyRepeating()))
                {
                case choice::Taking::Say:
                    speech::Now(words);
                    break;
                case choice::Taking::Cut:
                    speech::Cut(L"a side of the choice taken");
                    break;
                case choice::Taking::Wait:
                    // The key the side was taken with still repeats, and a screen reader stops at
                    // each repeat: the words, and what the game says after them, wait for it.
                    speech::Cut(L"a side of the choice taken, its key still down");
                    speech::Hold(L"choice");
                    log::Info(L"choices: the option taken waits for the player's key to be let go");
                    break;
                }
            }
            else if (end == choice::End::TimedOut)
            {
                sounds::Play(sounds::Cue::Fail);
                if (cfg::Detailed()) speech::Announce(locale::Mod(L"result.timeout"));
            }
        }

        // The game says how the choice ended. The event is hooked twice (the Blueprint's
        // override and the native it calls), and the first to arrive settles it.
        void OnEnded(UObject* self, FFrame& stack)
        {
            int64_t result = 0;
            params::Int(stack, L"ChoiceResult", result);
            Choice* choice = g_book.Of(self);
            if (!choice) return;
            Finish(*choice, result, L"told by " + obj::ObjectName(self));
        }

        void PollImpl()
        {
            const double now = gamethread::NowSeconds();
            // A side taken with a key still down: once it is let go, its words, then what the game
            // said meanwhile (choice::Chosen); the key is asked about only while it waits.
            if (g_chosen.Waiting())
            {
                const bool prompt = QteInProgress();
                if (const auto words = g_chosen.Due(now, keyhook::KeyRepeating(), prompt))
                {
                    log::Info(L"choices: the player's key is let go, or waited for long enough{}",
                              prompt ? L"; a quick-time event is up, and the option taken is told by its tone alone" : L"");
                    if (!words->empty()) speech::Now(*words);
                    speech::Release(L"choice");
                }
            }
            g_book.Prune([](choice::Handle widget) { return obj::IsLive(static_cast<UObject*>(widget)); });
            for (auto& choice : g_book.All())
            {
                if (choice::Book::DueToRead(choice, now)) Announce(choice);
            }
            // Each option widget keeps the game's verdict as well ("Choice Result", the
            // Blueprint's own copy of what its end event was told): a verdict the hooks did
            // not deliver is read from there.
            for (auto& choice : g_book.All())
            {
                if (choice.finished || !choice.announced) continue;
                for (const auto& o : choice.options)
                {
                    int64_t result = 0;
                    if (obj::ReadInt(WidgetOf(o), L"Choice Result", result) && result != 0)
                    {
                        Finish(choice, result, L"read from " + obj::ObjectName(WidgetOf(o)));
                        break;
                    }
                }
            }
            // The options hang in the world beside the character, not in the viewport, so
            // whether they are drawn cannot be asked of them; a choice whose option widgets
            // have all been destroyed without a verdict is over.
            for (auto& choice : g_book.All())
            {
                if (!choice::Book::Gone(choice, now)) continue;
                log::Info(L"choices: the options went away without a verdict");
                choice.finished = true;
            }
            g_book.Forget(now);
        }

        void Poll(float)
        {
            obj::SafeInvokeLogged(L"choices.PollImpl", [](void*) { PollImpl(); }, nullptr);
        }
    }

    void ChoicesFeature::Install()
    {
        hooks::OnScript(L"BatesDirectionalChoiceWidget", L"BP_Setup", &OnSetUp);
        // The game's events on the option widget are Blueprint overrides: the hold and the
        // clock never reach their native parents, the verdict does (the override calls it).
        hooks::OnScript(L"BatesDirectionalChoiceWidget", L"UpdateSelectionProgress", &OnSelection);
        hooks::OnScript(L"BatesDirectionalChoiceWidget", L"UpdateTimeoutProgress", &OnTimeout);
        hooks::OnScript(L"BatesDirectionalChoiceWidget", L"ChoiceEnded", &OnEnded);
        hooks::OnNative(L"/Script/Bates.BatesDirectionalChoiceWidget:UpdateSelectionProgress", &OnSelection, nullptr);
        hooks::OnNative(L"/Script/Bates.BatesDirectionalChoiceWidget:UpdateTimeoutProgress", &OnTimeout, nullptr);
        hooks::OnNative(L"/Script/Bates.BatesDirectionalChoiceWidget:ChoiceEnded", &OnEnded, nullptr);
        // A Blueprint event with no native body (no exec thunk in the PDB): the native class's stub calls
        // the widget Blueprint's own SetupGlobalStats, which only the script hook sees.
        hooks::OnScript(L"BatesDirectionalChoiceWidget", L"SetupGlobalStats", &OnGlobalStats);
        gamethread::AddPoller(L"choices", &Poll);
    }

    void ChoicesFeature::Describe(std::vector<std::wstring>& out)
    {
        for (const auto& choice : g_book.All())
        {
            if (choice.announced && !choice.finished) out.push_back(ChoiceText(choice));
        }
    }

    void ChoicesFeature::Help(std::vector<std::wstring>& out)
    {
        for (const auto& choice : g_book.All())
        {
            if (!choice.announced || choice.finished) continue;
            std::wstring left;
            std::wstring right;
            for (const auto& option : choice.options)
                (option.direction == 0 ? left : right) = KeyOf(option);
            if (!left.empty() && !right.empty()) out.push_back(locale::Mod(L"help.choice", left, right));
            return;
        }
    }

    bool ChoiceInProgress()
    {
        auto& choices = g_book.All();
        return std::any_of(choices.begin(), choices.end(), [](const choice::Choice& c) { return choice::InProgress(c); });
    }
}
