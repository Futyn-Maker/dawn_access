#pragma once
// The game's directional choices worked out without the game, so that they can be tested: which
// options make one choice, which choice a pooled option widget belongs to, when a hold toward an
// option begins, when a choice is read, how it ended, and when the option taken is said.

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

    // The option taken is said once the keys are released, so that it is not cut off by the
    // game's answer to the release; failing a release, after kChosenWait.
    constexpr double kChosenWait = 2.0;
    class Chosen
    {
    public:
        void Set(std::wstring text, double now);
        // Words wait to be said.
        bool Pending() const
        {
            return !m_text.empty();
        }
        // The words due now, with every key released or not; empty when none.
        std::wstring Due(double now, bool held);

    private:
        std::wstring m_text;
        double m_since = -1.0;
    };
}
