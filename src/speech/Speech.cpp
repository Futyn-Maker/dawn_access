#include "speech/Speech.hpp"

#include "core/Config.hpp"
#include "core/Fault.hpp"
#include "core/Gate.hpp"
#include "core/Log.hpp"
#include "core/Strings.hpp"
#include "input/InputNames.hpp"
#include "locale/Locale.hpp"
#include "speech/Outputs.hpp"

#include <chrono>
#include <deque>
#include <mutex>

namespace da::speech
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

        // Input this recent is treated as the reason for the utterance even if it landed
        // just after the previous one started.
        constexpr long long kFreshInputMs = 120;

        std::mutex g_mutex;
        std::wstring g_lastSpoken;
        std::wstring g_lastMessage; // the last thing said that was not itself a readout
        Clock::time_point g_lastSpokenAt{};
        Clock::time_point g_lastOutputAt{};                              // when the reader was last given something to say
        std::deque<std::wstring> g_history;                              // for Repeat()
        std::deque<std::pair<Clock::time_point, std::wstring>> g_recent; // what was said lately, for the audit
        constexpr long long kRecentMs = 120'000;
        bool g_preferSapi = false;

        // The lines kept while held, with their policies, and since when the hold stands: a
        // hold nobody lets go of (its mechanic gone without an ending) lapses by itself.
        bool g_held = false;
        Clock::time_point g_heldAt{};
        std::deque<std::pair<std::wstring, std::wstring>> g_heldLines;
        constexpr long long kHoldLapseMs = 15'000;
        constexpr size_t kHeldMost = 12;

        // SAPI speaks when no screen reader runs, and when the player prefers it to theirs.
        bool SapiSpeaks()
        {
            if (!outputs::SapiAvailable()) return false;
            return g_preferSapi || outputs::ReaderName().empty();
        }

        // One line to whichever speaks. A screen reader with a display keeps getting the
        // braille while SAPI does the speaking, which the outputs see to.
        void Emit(const std::wstring& text, bool interrupt)
        {
            outputs::Say(text, interrupt, SapiSpeaks());
        }

        long long MsSince(Clock::time_point t)
        {
            if (t.time_since_epoch().count() == 0) return 1'000'000;
            return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t).count();
        }

        void Remember(std::wstring_view text)
        {
            g_lastSpoken = std::wstring(text);
            g_lastSpokenAt = Clock::now();
            g_history.emplace_back(text);
            while (g_history.size() > 10)
                g_history.pop_front();
        }

        void OutputNow(std::wstring_view text, bool interrupt, const wchar_t* policy)
        {
            log::Say(policy, text);
            Emit(std::wstring(text), interrupt);
            g_lastOutputAt = Clock::now();
            Remember(text);
            g_recent.emplace_back(Clock::now(), str::CollapseWhitespace(text));
            while (!g_recent.empty() && MsSince(g_recent.front().first) > kRecentMs)
                g_recent.pop_front();
            if (std::wstring_view(policy) != L"now") g_lastMessage = std::wstring(text);
        }

        // Cutting a sentence short is only right when the player is waiting for the answer
        // to something they just did: they pressed, held or moved something after the
        // current utterance began. Everything the game says by itself waits its turn.
        bool PlayerIsWaiting()
        {
            const long long sinceInput = input::MsSinceInput();
            return sinceInput < kFreshInputMs || sinceInput <= MsSince(g_lastOutputAt);
        }

        // Every line is cleaned once, here: the game's markup stripped and its whitespace
        // collapsed, so that no reader has to, and a line that is nothing but markup is not
        // said at all.
        std::wstring Clean(std::wstring_view text)
        {
            return str::Trim(str::StripMarkup(text));
        }

        // Whether a line is said now: the mod has a voice and is switched on. Off, the line
        // goes to the log at Verbose and no further.
        bool Says(std::wstring_view text)
        {
            if (!outputs::CanSpeak()) return false;
            if (gate::IsOn()) return true;
            log::Verbose(L"speech: dropped while the mod is off: \"{}\"", text);
            return false;
        }

        // Says the lines kept by a hold, in order. Called under the lock.
        void FlushHeld()
        {
            g_held = false;
            while (!g_heldLines.empty())
            {
                const auto [policy, text] = g_heldLines.front();
                g_heldLines.pop_front();
                OutputNow(text, false, policy.c_str());
            }
        }

        // Whether a line the game says by itself is kept for later. Called under the lock.
        bool KeptByHold(const std::wstring& text, const wchar_t* policy)
        {
            if (!g_held) return false;
            if (MsSince(g_heldAt) > kHoldLapseMs)
            {
                log::Info(L"speech: the hold lapsed after {} s", kHoldLapseMs / 1000);
                FlushHeld();
                return false;
            }
            if (g_heldLines.size() >= kHeldMost) g_heldLines.pop_front();
            g_heldLines.emplace_back(policy, text);
            log::Verbose(L"speech: held \"{}\"", text);
            return true;
        }
    }

    void Init()
    {
        fault::Guard lock(g_mutex, L"speech");
        g_lastSpoken.clear();
        g_lastMessage.clear();
    }

    void Focus(std::wstring_view raw)
    {
        const std::wstring text = Clean(raw);
        if (text.empty() || !Says(text)) return;
        fault::Guard lock(g_mutex, L"speech");
        if (text == g_lastSpoken && MsSince(g_lastSpokenAt) < cfg::Get().focusDedupeMs)
        {
            log::Trace(L"speech: focus dedupe \"{}\"", text);
            return;
        }
        if (!PlayerIsWaiting())
        {
            OutputNow(text, false, L"focus-after");
            return;
        }
        OutputNow(text, true, L"focus");
    }

    void Announce(std::wstring_view raw)
    {
        const std::wstring text = Clean(raw);
        if (text.empty() || !Says(text)) return;
        fault::Guard lock(g_mutex, L"speech");
        if (KeptByHold(text, L"announce")) return;
        OutputNow(text, false, L"announce");
    }

    void Hold()
    {
        fault::Guard lock(g_mutex, L"speech");
        if (g_held) return;
        g_held = true;
        g_heldAt = Clock::now();
        log::Verbose(L"speech: what the game says by itself is held");
    }

    void Release()
    {
        fault::Guard lock(g_mutex, L"speech");
        if (!g_held) return;
        log::Verbose(L"speech: the hold is let go, with {} line(s) kept", g_heldLines.size());
        FlushHeld();
    }

    void Exclusive(std::wstring_view raw)
    {
        const std::wstring text = Clean(raw);
        if (text.empty() || !Says(text)) return;
        fault::Guard lock(g_mutex, L"speech");
        OutputNow(text, false, L"exclusive");
    }

    void Now(std::wstring_view raw)
    {
        const std::wstring text = Clean(raw);
        if (text.empty() || !Says(text)) return;
        fault::Guard lock(g_mutex, L"speech");
        OutputNow(text, true, L"now");
    }

    void Urgent(std::wstring_view raw)
    {
        const std::wstring text = Clean(raw);
        if (text.empty() || !Says(text)) return;
        fault::Guard lock(g_mutex, L"speech");
        OutputNow(text, true, L"urgent");
    }

    void Replace(std::wstring_view raw)
    {
        const std::wstring text = Clean(raw);
        if (text.empty() || !Says(text)) return;
        fault::Guard lock(g_mutex, L"speech");
        OutputNow(text, true, L"replace");
    }

    void Stop()
    {
        fault::Guard lock(g_mutex, L"speech");
        outputs::Silence();
        g_heldLines.clear();
        log::Info(L"speech: stopped");
    }

    void StopForKey(std::wstring_view key)
    {
        fault::Guard lock(g_mutex, L"speech");
        outputs::Silence();
        log::Verbose(L"speech: stopped by {}", key);
    }

    void Repeat()
    {
        fault::Guard lock(g_mutex, L"speech");
        if (g_history.empty() || !gate::IsOn()) return;
        const std::wstring last = g_history.back();
        log::Say(L"repeat", last);
        Emit(last, true);
        g_lastOutputAt = Clock::now();
    }

    std::wstring Last()
    {
        fault::Guard lock(g_mutex, L"speech");
        return g_lastMessage;
    }

    bool SaidRecently(std::wstring_view raw, double seconds)
    {
        const std::wstring text = str::CollapseWhitespace(Clean(raw));
        if (text.empty()) return true;
        fault::Guard lock(g_mutex, L"speech");
        for (auto it = g_recent.rbegin(); it != g_recent.rend(); ++it)
        {
            if (MsSince(it->first) > static_cast<long long>(seconds * 1000.0)) break;
            if (it->second.find(text) != std::wstring::npos || text.find(it->second) != std::wstring::npos) return true;
        }
        return false;
    }

    void ToggleOutput()
    {
        fault::Guard lock(g_mutex, L"speech");
        const std::wstring reader = outputs::ReaderName();
        // Without a screen reader the choice makes no difference: SAPI speaks either way.
        if (reader.empty() || !outputs::SapiAvailable())
        {
            OutputNow(locale::Mod(L"speech.noreader"), true, L"now");
            return;
        }
        g_preferSapi = !g_preferSapi;
        outputs::Silence();
        log::Info(L"speech: output {} (SAPI preferred: {}; SAPI voice {})", g_preferSapi ? L"SAPI" : reader, g_preferSapi, outputs::SapiVoiceName());
        if (!cfg::Persist(L"Speech", L"PreferSapi", g_preferSapi ? L"1" : L"0")) log::Error(L"speech: the output setting could not be saved to DawnAccess.ini");
        OutputNow(locale::Mod(L"speech.output", g_preferSapi ? L"SAPI" : reader), true, L"now");
    }

    void SetPreferSapi(bool prefer)
    {
        fault::Guard lock(g_mutex, L"speech");
        g_preferSapi = prefer;
    }

    std::wstring OutputName()
    {
        fault::Guard lock(g_mutex, L"speech");
        if (SapiSpeaks()) return L"SAPI";
        return outputs::ReaderName();
    }
}
