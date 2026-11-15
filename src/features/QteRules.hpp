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

    // An example the game ends with no verdict and leaves drawn stands on its page, as a prompt paused
    // in play stands until the press (whose key is said every kRepeat meanwhile: research/sessions/
    // s45-full.log, 23:44:42): the settings' "pause on timeout" runs its example a second, ends it with
    // none and leaves it up until the value or the row changes (2026-11-15, 20:15:05.591 to 20:15:13.009:
    // WBP_QTEPrompt_C_2147476619), and the demonstration turned on in that time found nothing left to
    // voice, the example taken for gone once it ended. Every other mode's example ends with a verdict (timed
    // out, succeeded) or is taken from the page, and a prompt in play never stands so.
    bool Stands(bool example, long long result, bool drawn);

    // ---- the settings' demonstration ---------------------------------------------------------
    //
    // The accessibility tab's QTE rows (Setting_QTEBehaviour, Setting_QTEIcon, Setting_SingleQTEAction,
    // Setting_QTEScale, Setting_QTEPosition) run the game's own example of a QTE beside them while one
    // of them has the focus, as the setting shapes it: in the standard mode a prompt every two seconds
    // that runs a second and times out, with "succeed on timeout" one that succeeds, with "pause on
    // timeout" one that runs a second and stands, in the automatic modes none (their prompt is not shown
    // in play either). Said as it ran, each prompt and the copies the page sets up with it buried the
    // rows under "cross" every two seconds as the player went through the values (research/sessions/
    // s73-play.log, 11:17:01 to 11:24:23). So the examples are silent, and the row says after the game's
    // own words which key plays them (Andrey, 2026-11-13): the key turns the example's voice on and off,
    // for every row of the group, and it stops by itself when the focus leaves the row or its value
    // changes. Playing, an example is voiced as a prompt in play is: its cue and key, its cue again as
    // it takes the press, the key every second, and the tone of how it ended.
    bool DemoRow(std::wstring_view controlName);
    class Demo
    {
    public:
        // The row the menu's focus is on and its value, as a poll reads them (an empty row: none of the
        // group): true when a demonstration playing stops for it.
        bool Focus(std::wstring_view row, std::wstring_view value);
        enum class Toggle
        {
            On,
            Off,
            NotHere, // no row of the group has the focus
        };
        Toggle Toggled();
        bool Playing() const
        {
            return m_playing;
        }
        // A row of the group has the focus: the key is the demonstration's.
        bool Here() const
        {
            return DemoRow(m_row);
        }
        // The demonstration ends wherever it stood: the menu left.
        void Stop()
        {
            m_playing = false;
        }

    private:
        std::wstring m_row;
        std::wstring m_value;
        bool m_playing = false;
    };
}
