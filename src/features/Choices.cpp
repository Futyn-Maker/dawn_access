#include "features/Choices.hpp"

#include "core/Config.hpp"
#include "core/GameThread.hpp"
#include "core/Log.hpp"
#include "core/ObjectUtil.hpp"
#include "core/ParamReader.hpp"
#include "core/Strings.hpp"
#include "hooks/HookDispatcher.hpp"
#include "input/InputNames.hpp"
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
        // The game's own words for how a choice ended.
        constexpr int64_t kResultLeft = 1;
        constexpr int64_t kResultRight = 2;
        constexpr int64_t kResultTimedOut = 3;

        // One option of a choice: the widget the game set up with its words.
        struct Option
        {
            UObject* widget = nullptr;
            int64_t direction = 0; // 0 left, 1 right
            std::wstring text;
            std::wstring subText;
            bool timed = false;
            double selection = 0.0; // how far the player has held toward it
            bool heldSaid = false;  // its words said as the player began to hold toward it
        };

        // A choice: the options set up together.
        struct Choice
        {
            std::vector<Option> options;
            double firstSetUpAt = 0.0;
            bool announced = false;
            bool finished = false;
            double timeout = 0.0; // how far the clock has run
        };
        std::vector<Choice> g_choices;
        std::wstring g_pendingChosen; // the option taken, said once every key is released
        double g_pendingSince = -1.0;

        Choice* Current()
        {
            return g_choices.empty() ? nullptr : &g_choices.back();
        }

        Choice* ChoiceOf(UObject* widget, Option** option)
        {
            for (auto& choice : g_choices)
            {
                for (auto& o : choice.options)
                {
                    if (o.widget == widget)
                    {
                        if (option) *option = &o;
                        return &choice;
                    }
                }
            }
            return nullptr;
        }

        const wchar_t* Side(int64_t direction)
        {
            return direction == 0 ? L"dir.left" : L"dir.right";
        }

        // The key an option is taken with, as the game draws it beside the option.
        std::wstring KeyOf(const Option& option)
        {
            std::vector<std::wstring> keys;
            for (const auto& prompt : ui::PromptsIn(option.widget))
            {
                if (!prompt.key.empty()) keys.push_back(prompt.key);
            }
            return str::Join(keys, L", ");
        }

        std::wstring OptionWords(const Option& option)
        {
            std::vector<std::wstring> parts;
            if (!option.text.empty()) parts.push_back(option.text);
            if (!option.subText.empty()) parts.push_back(option.subText);
            return str::JoinSentences(parts);
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
                if (!obj::IsLive(option.widget)) continue;
                const auto words = OptionWords(option);
                if (words.empty()) continue;
                std::wstring line = locale::Mod(Side(option.direction)) + L": " + words;
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

        // The game set an option up with its words. Options set up within a moment of each
        // other are one choice; the choice is read once both sides are in, or after a moment
        // should the second never come.
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
            Choice* choice = Current();
            const bool joins = choice && !choice->announced && !choice->finished && now - choice->firstSetUpAt < 1.5 &&
                               std::none_of(choice->options.begin(), choice->options.end(), [&](const Option& o) { return o.direction == option.direction; });
            if (!joins)
            {
                g_choices.push_back(Choice{});
                choice = &g_choices.back();
                choice->firstSetUpAt = now;
            }
            // The same widget set up anew carries the new words.
            std::erase_if(choice->options, [&](const Option& o) { return o.widget == self; });
            choice->options.push_back(option);
            if (g_choices.size() > 8) g_choices.erase(g_choices.begin());
        }

        // The player holds toward an option: its words are said as the hold begins.
        void OnSelection(UObject* self, FFrame& stack)
        {
            double progress = 0.0;
            params::Float(stack, L"InProgress", progress);
            Option* option = nullptr;
            Choice* choice = ChoiceOf(self, &option);
            if (!choice || !option || choice->finished) return;
            const bool began = progress > 0.05 && option->selection <= 0.05;
            option->selection = progress;
            if (began && !option->heldSaid)
            {
                option->heldSaid = true;
                for (auto& other : choice->options)
                {
                    if (other.widget != self) other.heldSaid = false;
                }
                const auto words = OptionWords(*option);
                log::Verbose(L"choices: holding toward \"{}\"", words);
                if (!words.empty()) speech::Focus(words);
            }
            // The ring the hold fills on the option stays silent: the words at the start of
            // the hold and the tone at the end are the whole of it, as a held key keeps the
            // reader silent in between anyway.
        }

        void OnTimeout(UObject* self, FFrame& stack)
        {
            double progress = 0.0;
            params::Float(stack, L"InProgress", progress);
            if (Choice* choice = ChoiceOf(self, nullptr)) choice->timeout = progress;
        }

        // What the game's verdict means: the side taken, with its tone and its word once the
        // keys are released; the time run out; or an end of the stage's own, without a word.
        void Finish(Choice& choice, int64_t result, const std::wstring& how)
        {
            if (choice.finished || result == 0) return;
            choice.finished = true;
            log::Info(L"choices: ended with {} ({})", result, how);
            if (result == kResultLeft || result == kResultRight)
            {
                sounds::Play(sounds::Cue::Confirm);
                const int64_t direction = result == kResultLeft ? 0 : 1;
                for (const auto& o : choice.options)
                {
                    if (o.direction == direction && !o.text.empty())
                    {
                        g_pendingChosen = locale::Mod(L"choice.chosen", o.text);
                        g_pendingSince = gamethread::NowSeconds();
                    }
                }
            }
            else if (result == kResultTimedOut)
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
            Choice* choice = ChoiceOf(self, nullptr);
            if (!choice) return;
            Finish(*choice, result, L"told by " + obj::ObjectName(self));
        }

        void PollImpl()
        {
            const double now = gamethread::NowSeconds();
            // The option taken is told once the keys are released, so that it is not cut off
            // by the game's answer to the release; failing a release, after a moment.
            if (!g_pendingChosen.empty() && (!input::InputHeld() || now - g_pendingSince > 2.0))
            {
                if (cfg::Detailed()) speech::Announce(g_pendingChosen);
                g_pendingChosen.clear();
            }
            for (auto& choice : g_choices)
            {
                std::erase_if(choice.options, [](const Option& o) { return !obj::IsLive(o.widget); });
                if (choice.finished || choice.announced) continue;
                if (choice.options.empty()) continue;
                const bool bothIn = choice.options.size() >= 2;
                if (bothIn || now - choice.firstSetUpAt >= 2.0) Announce(choice);
            }
            // Each option widget keeps the game's verdict as well ("Choice Result", the
            // Blueprint's own copy of what its end event was told): a verdict the hooks did
            // not deliver is read from there.
            for (auto& choice : g_choices)
            {
                if (choice.finished || !choice.announced) continue;
                for (const auto& o : choice.options)
                {
                    int64_t result = 0;
                    if (obj::ReadInt(o.widget, L"Choice Result", result) && result != 0)
                    {
                        Finish(choice, result, L"read from " + obj::ObjectName(o.widget));
                        break;
                    }
                }
            }
            // The options hang in the world beside the character, not in the viewport, so
            // whether they are drawn cannot be asked of them; a choice whose option widgets
            // have all been destroyed without a verdict is over.
            for (auto& choice : g_choices)
            {
                if (choice.finished || !choice.announced) continue;
                if (choice.options.empty() && now - choice.firstSetUpAt > 2.0)
                {
                    log::Info(L"choices: the options went away without a verdict");
                    choice.finished = true;
                }
            }
            std::erase_if(g_choices, [&](const Choice& c) { return c.finished && now - c.firstSetUpAt > 30.0; });
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
        gamethread::AddPoller(L"choices", &Poll);
    }

    void ChoicesFeature::Describe(std::vector<std::wstring>& out)
    {
        for (const auto& choice : g_choices)
        {
            if (choice.announced && !choice.finished) out.push_back(ChoiceText(choice));
        }
    }

    void ChoicesFeature::Help(std::vector<std::wstring>& out)
    {
        for (const auto& choice : g_choices)
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
}
