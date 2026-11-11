#pragma once
// The game's directional choices worked out without the game, so that they can be tested: which
// options make one choice, which choice a pooled option widget belongs to, when a hold toward an
// option begins, when a choice is read, how it ended, and when the option taken is said.

#include <optional>
#include <string>
#include <vector>

namespace da::choice
{
    // An option widget of the game's, opaque here.
    using Handle = void*;

    // How the game says a choice ended (EBatesDirectionalChoiceResult): 0 not yet, 1 the left
    // option, 2 the right, 3 the time run out; anything else is an end of the stage's own, which
    // ends the choice without a word.
    enum class End
    {
        None,
        Left,
        Right,
        TimedOut,
        Other,
    };
    End EndOf(long long result);

    // One option of a choice: the widget the game set up with its words.
    struct Option
    {
        Handle widget = nullptr;
        long long direction = 0; // 0 left, 1 right
        std::wstring text;
        std::wstring subText;
        bool timed = false;
        double selection = 0.0; // how far the player has held toward it
        double peak = 0.0;      // the most the ring has filled since this hold began
        bool emptying = false;  // the ring has run a good part of the way down since
        // The share of players who took it, as the game shows it beside the option once the game is
        // finished and its setting is on (UBatesDirectionalChoiceWidget::SetupGlobalStats, called as
        // the choice starts); below 0 while none is shown.
        double globalShare = -1.0;
    };

    // An option's share of the players who took it, as said: a whole percent, from the game's own
    // share (a fraction of one, UBatesDirectionalChoiceStage::OnShowGlobalStatsChanged's quotient).
    std::wstring SharePercent(double share);

    // An option's own words: its text, then its second line.
    std::wstring Words(const Option& option);
    // What is said as a hold toward an option begins: its words, then the share of players who took
    // it where the game shows one, as the reading of the whole choice says it (Andrey, 2026-11-10: a
    // press toward an option names it with its share, the hold that takes it as well).
    std::wstring HeldWords(const Option& option);

    // A choice: the options set up together.
    struct Choice
    {
        std::vector<Option> options;
        double firstSetUpAt = 0.0;
        bool announced = false;
        bool finished = false;
        double timeout = 0.0; // how far the clock has run
    };

    // The player holds toward an option: true as each hold begins, as the ring starts to fill:
    // from empty, or again after it had run a good part of the way down (the trigger let go and
    // pressed again before the ring emptied; the game lets the ring down at a steady pace
    // whenever the trigger is let go or eases). The game reports every moment twice, its
    // Blueprint and the native under it with the same value, and a hold's own small ups and
    // downs are the same hold.
    bool Hold(Option& option, double progress);

    // Options set up within kJoinWithin of each other, one per side, are one choice; a choice is
    // read once both sides are in, or kReadAfter after its first option should the second never
    // come; one whose options all went away without a verdict is over kReadAfter after it began;
    // a finished one is forgotten after kForgetAfter; at most kChoicesKept are kept.
    constexpr double kJoinWithin = 1.5;
    constexpr double kReadAfter = 2.0;
    constexpr double kForgetAfter = 30.0;
    constexpr size_t kChoicesKept = 8;

    class Book
    {
    public:
        // The game set an option up with its words. The same widget set up anew carries the new
        // words, and belongs to this choice alone: the game's two option widgets are pooled and
        // serve one choice after another, and an earlier, finished choice still listing them took
        // the presses and the verdict of the next ones until it was dropped half a minute later.
        void SetUp(const Option& option, double now);
        // The choice an option widget belongs to now: the latest that lists it.
        Choice* Of(Handle widget, Option** option = nullptr);
        // The verdict: what it means, once (None for no verdict yet, or a choice already
        // finished); with a side taken, `chosen` gets that option's words.
        End Finish(Choice& choice, long long result, std::wstring* chosen = nullptr);
        // Drops the options whose widgets are gone.
        template <typename Alive>
        void Prune(Alive alive)
        {
            for (auto& c : m_choices)
                std::erase_if(c.options, [&](const Option& o) { return !alive(o.widget); });
        }
        // A choice to read now.
        static bool DueToRead(const Choice& c, double now);
        // A read choice whose options all went away without a verdict.
        static bool Gone(const Choice& c, double now);
        // Forgets the choices finished long enough ago.
        void Forget(double now);
        std::vector<Choice>& All()
        {
            return m_choices;
        }

    private:
        std::vector<Choice> m_choices;
    };

    // True while a choice is up and not yet settled.
    bool InProgress(const Choice& c);

    // What the player hears as a side is taken (Andrey, 2026-11-11): the taking cuts whatever is
    // being said, at either level. The option's words, read as the hold toward it began, ran on
    // past it, and the game's status card waited behind them, so that at the detailed level the
    // option taken came last: "УВЕРЕННОСТЬ. Как дела?", "Статус обновлен: R1", "УВЕРЕННОСТЬ,
    // выбрано." (research/sessions/s69-play.log, 10:43:37 to 10:43:38). Now the detailed level
    // says "X, chosen." at once in their place, and what the game says after it follows it; the
    // brief level falls silent, and the next thing said is the game's. A keyboard key still down
    // for the game cuts a screen reader at every repeat (NVDA's speech interrupt for typed
    // characters), so while one is held at the taking, the words, and everything the game says
    // after them, wait for its release, at most kChosenWait.
    constexpr double kChosenWait = 2.0;
    enum class Taking
    {
        Say,  // the words, cutting what is being said
        Cut,  // silence: the brief level has no words for it
        Wait, // silence; the words and what the game says after them wait for the key's release
    };
    class Chosen
    {
    public:
        // A side taken at `now`, with the words to say (empty at the brief level) and whether a key
        // is down for the game.
        Taking Take(std::wstring words, double now, bool keyRepeating);
        // A taking waits for the key's release.
        bool Waiting() const
        {
            return m_waiting;
        }
        // The end of the wait, once the key is let go or kChosenWait after the taking: the words to
        // say then (empty at the brief level). Nothing while it still waits, or when none does. A
        // quick-time event's prompt up by then has cut in over everything (the prologue starts one
        // at once after a choice), and the words, said now, would cut its key: they are dropped,
        // the side taken told by its tone alone.
        std::optional<std::wstring> Due(double now, bool keyRepeating, bool promptUp);

    private:
        std::wstring m_words;
        double m_since = -1.0;
        bool m_waiting = false;
    };
}
