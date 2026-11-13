#pragma once
// A quick-time event worked out without the game, so that it can be tested: how its prompt
// ends, when its key is said again, the direction its cue plays, and which examples are said.

#include <string>
#include <string_view>

namespace da::qte
{
    // The game's verdicts (EBatesQTEResult): 1 success, 2 failure, 3 timeout. Anything else is an
    // end with no verdict (the game left its menu or moved on first), which the prompt shows
    // nothing for, so nothing is said or played for it either.
    enum class Outcome
    {
        None,
        Success,
        Failure,
        Timeout,
    };
    Outcome OutcomeOf(long long result);

    // The direction a prompt points, from the name of the action the game asks for: a stick
    // pushed up, down, left or right, or a face button, which sits at the top, the bottom, the
    // left or the right of the pad, as its keys W, S, A and D do. The direction is the last word
    // of the name: the stick's own side comes before it ("IA_QTELeftStickRight" is right).
    enum class Direction
    {
        Up,
        Down,
        Left,
        Right,
    };
    Direction DirectionOf(std::wstring_view action);

    // The same set-up told twice (the game's own event after its call): the same action within
    // kSameSetUp.
    constexpr double kSameSetUp = 0.5;
    bool SameSetUp(std::wstring_view knownAction, double knownAt, std::wstring_view action, double now);

    // A prompt as the poll weighs it.
    struct Prompt
    {
        double setUpAt = 0.0;
        double spokenAt = -10.0; // when its key was last said (or its cue played as it opened)
        bool paused = false;
        bool finished = false;
        bool demonstration = false; // a prompt a settings page shows as an example
        double hiddenSince = -1.0;  // since when the prompt has not been drawn
        // The game's warm-up (InWarmupDuration): the prompt is drawn, and a press is not taken
        // (UBatesQTEStage::ProcessQTEPressed counts one only once the stage runs), until it is over;
        // `opened`, the game has begun taking the press (its progress at the Main stage).
        double warmup = 0.0;
        bool opened = false;
    };

    // Whether a prompt is up and running in play: what the game says by itself waits while one is
    // (speech::Hold), since a word in the same moment drowns the key.
    bool Running(const Prompt& p);

    // The activity's QTE stage that drives a prompt, as read at a poll.
    struct Stage
    {
        bool known = false;   // the prompt was set up with a stage of its own (it may be gone since)
        bool live = false;    // and that stage is still there
        int verdict = 0;      // its verdict (QTEResult)
        bool drivesIt = true; // its widget is still this prompt
        bool started = true;  // it has begun (bHasStarted)
    };

    // The key is said again every kRepeat while the prompt runs. A prompt not drawn for
    // kHiddenWithStage after its first kFirstSecond is over (at once without a stage, whose
    // verdict would have told it).
    // The key is said as the prompt is set up, its cue again as the game begins taking the press
    // after a warm-up, and every kRepeat once the press is taken; a warm-up says nothing more, since a
    // press in it is not taken. Of the game's 201 QTE stages 136 set a warm-up of their own, 55 of them
    // 2 s or more (research/mechanics/qte_stages.tsv), and the key said every second through it asked
    // for presses the game would not take.
    constexpr double kRepeat = 1.0;
    // A prompt taking the press within kOpeningApart of its set-up had its cue at the set-up: no second
    // cue on top of it ("takes the press from 0.00 s" in nearly every prompt of s73).
    constexpr double kOpeningApart = 0.15;
    bool CuesOpening(double sinceSetUp);
    constexpr double kFirstSecond = 1.0;
    constexpr double kHiddenWithStage = 1.5;

    struct Step
    {
        bool finish = false;
        long long result = 0;      // the verdict it ends with, 0 for none
        const wchar_t* from = L""; // what told it, for the log
        bool repeat = false;       // the key is said again now
    };

    // One poll of an unfinished prompt. The stage that drives it says how it ended: its verdict,
    // which the widget's own event has usually told already, or its end with none (its widget
    // handed back, or the stage over). The prompt hangs on the canvas that follows the world, in
    // no widget tree, so its own flags are asked only when the stage is not known.
    Step Poll(Prompt& p, double now, bool drawn, const Stage& stage);

    // The examples a settings page shows: the same one shown in several places at once is said
    // once, within kExamplesApart.
    constexpr double kExamplesApart = 0.5;
    class Examples
    {
    public:
        // Whether an example coming up now is said.
        bool Says(const std::wstring& text, double now);

    private:
        std::wstring m_last;
        double m_at = -10.0;
    };

    // An example is said once it has stood kExampleSettle, still drawn on its page: a settings page
    // the player leaves sets up the example of the row its focus comes back to as it goes (the
    // settings saved at act2_1: the QTE scale row's example came up and went in 52 ms, and its key
    // and cue were heard over the pause menu, 2026-11-02).
    constexpr double kExampleSettle = 0.3;
    enum class ExampleStep
    {
        Wait,
        Say,
        Drop, // gone before it was said
    };
    ExampleStep Example(double setUpAt, double now, bool shown);
}
