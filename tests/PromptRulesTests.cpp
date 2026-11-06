// features/PromptRules: when a prompt's question is new and when it is said, which lines of the
// action bar are said, and the blips of the phone's unlock swipe.
#include "Support.hpp"
#include "features/PromptRules.hpp"

#include <string>
#include <vector>

using namespace da;

namespace
{
    // The prompts poller runs every second frame: a poll every 1/16 s keeps the sums exact.
    constexpr double kPoll = 1.0 / 16.0;

    // Polls one readout from `from` for `polls` polls; the times it was said.
    std::vector<double> Reads(prompt::Question& q, double from, int polls, const std::wstring& text, const std::wstring& actions, bool glint)
    {
        std::vector<double> said;
        for (int i = 0; i < polls; ++i)
        {
            const double now = from + i * kPoll;
            if (prompt::Read(q, now, text, actions, glint)) said.push_back(now);
        }
        return said;
    }
}

TEST_CASE("prompt: a readout is said once it has stood, a glint's longer than an interaction's")
{
    prompt::Question interaction;
    CHECK(Reads(interaction, 0.0, 10, L"Door: cross", L"IA_StartInteraction", false) == std::vector<double>{0.125});
    prompt::Question glint;
    CHECK(Reads(glint, 0.0, 10, L"Door: cross", L"IA_StartInteraction", true) == std::vector<double>{0.4375});
}

TEST_CASE("prompt: a glint's flicker of 150 ms is no question")
{
    // The lodge, 2026-10-24: the press authorised for a moment as she walked through.
    prompt::Question glint;
    CHECK(Reads(glint, 0.0, 3, L"Totem: cross", L"IA_StartInteraction", true).empty());
    CHECK(Reads(glint, 0.1875, 10, L"", L"", true).empty());
}

TEST_CASE("prompt: the same words for another action are another question")
{
    prompt::Question q;
    CHECK(Reads(q, 0.0, 4, L"left stick", L"IA_Movement", false).size() == 1);
    CHECK(Reads(q, 0.25, 4, L"left stick", L"IA_InteractionMovement_Up", false).size() == 1);
}

TEST_CASE("prompt: a question set up after the last went away is new, even in the same words")
{
    prompt::Question q;
    CHECK(Reads(q, 0.0, 4, L"left stick", L"IA_Movement", false).size() == 1);
    // Set up again while it stays: the same question still.
    prompt::SetUp(q, 0.5);
    CHECK(Reads(q, 0.5, 8, L"left stick", L"IA_Movement", false).empty());
    // Sent away, then set up anew with the same words: asked again.
    prompt::Hide(q);
    prompt::SetUp(q, 2.0);
    CHECK(Reads(q, 2.0, 4, L"left stick", L"IA_Movement", false) == std::vector<double>{2.125});
}

TEST_CASE("prompt: a glint's prompt withheld while the character is out of the player's hands")
{
    prompt::Question q;
    CHECK(Reads(q, 0.0, 10, L"Door: cross", L"IA_StartInteraction", true).size() == 1);
    // Still up while the mod walks her: the same prompt, not said again afterwards.
    prompt::Withheld(q, true);
    CHECK(Reads(q, 1.0, 10, L"Door: cross", L"IA_StartInteraction", true).empty());
    // Gone down meanwhile: its next showing is new.
    prompt::Withheld(q, false);
    CHECK(Reads(q, 2.0, 10, L"Door: cross", L"IA_StartInteraction", true).size() == 1);
}

TEST_CASE("prompt: the action bar says its new lines once they stand, and keeps what play left while a page is up")
{
    prompt::Bar bar;
    CHECK(bar.Update(0.0, false, {L"Skip: cross"}).empty());
    CHECK(bar.Update(0.0625, false, {L"Skip: cross"}).empty());
    CHECK(bar.Update(0.125, false, {L"Skip: cross"}) == std::vector<std::wstring>{L"Skip: cross"});
    CHECK(bar.Update(0.25, false, {L"Skip: cross"}).empty());
    // A page over it: nothing, and the bar as play left it stays.
    CHECK(bar.Update(1.0, true, {}).empty());
    CHECK(bar.Update(2.0, false, {L"Skip: cross"}).empty());
    // A line added: only it is said; a line that left and came back is said again.
    bar.Update(3.0, false, {L"Skip: cross", L"Back: circle"});
    CHECK(bar.Update(3.125, false, {L"Skip: cross", L"Back: circle"}) == std::vector<std::wstring>{L"Back: circle"});
    bar.Update(4.0, false, {L"Back: circle"});
    CHECK(bar.Update(4.125, false, {L"Back: circle"}).empty());
    bar.Update(5.0, false, {L"Skip: cross", L"Back: circle"});
    CHECK(bar.Update(5.125, false, {L"Skip: cross", L"Back: circle"}) == std::vector<std::wstring>{L"Skip: cross"});
}

TEST_CASE("prompt: the phone's swipe: blips as it fills and slides back, the unlock after the swipe")
{
    // The prologue's phone: 50 to fill, 5 a swipe.
    prompt::Swipe swipe;
    auto step = swipe.Update(0.0, 1, 0.0, 50.0);
    CHECK(step.stateChanged);
    CHECK_FALSE(step.began);
    step = swipe.Update(0.1, 2, 0.0, 50.0);
    CHECK(step.began);
    CHECK_FALSE(step.blip);
    std::vector<double> levels;
    double progress = 0.0;
    for (int i = 1; i <= 6; ++i)
    {
        progress += 5.0;
        step = swipe.Update(0.1 + 0.1 * i, 2, progress, 50.0);
        if (step.blip) levels.push_back(step.level);
    }
    CHECK(levels.size() == 6);
    // Sliding back, a blip as it falls each tenth.
    step = swipe.Update(1.0, 2, 24.0, 50.0);
    CHECK(step.blip);
    CHECK(step.level == doctest::Approx(0.48));
    // Too soon after a blip: none.
    CHECK_FALSE(swipe.Update(1.05, 2, 15.0, 50.0).blip);
    step = swipe.Update(1.2, 3, 50.0, 50.0);
    CHECK(step.unlocked);
    // Out of the pocket again on another phone: no unlock without its swipe.
    swipe.Reset();
    CHECK_FALSE(swipe.Update(2.0, 3, 0.0, 50.0).unlocked);
}

TEST_CASE("prompt: a key cap set up with nothing shows no key, 2026-11-05")
{
    // At the mine on the keyboard (research/sessions/s44.log 20:48:22): the cigar stub's prompt had
    // its grab in the second cap and nothing in the first, which kept the D of the totem's "W, S, A,
    // D" and was read "D, left mouse button"; after each clue the empty cap alone was read "D".
    CHECK(prompt::CapShowsKey(true, false));
    CHECK_FALSE(prompt::CapShowsKey(false, false));
    // The controls page's rebinding rows set their caps up with a key.
    CHECK(prompt::CapShowsKey(false, true));
}

TEST_CASE("prompt: the first question of an interaction waits for the hint the game shows a moment after it, 2026-11-05")
{
    // The fortune totem on the keyboard (research/sessions/s44.log): taken into the interaction at
    // 20:45:22.63, the totem held at 26.68 with its stick and back prompts, the hint drawn at 27.11
    // and said at 27.35. The keys were said at 26.81, before the hint.
    prompt::HintFirst h;
    h.Interaction(false);
    h.Interaction(true);                 // 22.63
    CHECK_FALSE(h.Waits(26.0, -1000.0)); // nothing asked yet
    h.Asked(26.68);
    CHECK(h.Waits(26.81, -1000.0)); // where the keys were said: they wait
    CHECK(h.Waits(27.20, -1000.0));
    CHECK_FALSE(h.Waits(27.36, 27.35)); // the hint said: the keys follow it
    // No hint comes (the generator's first grab): the keys wait out the moment and no longer.
    prompt::HintFirst g;
    g.Interaction(true);
    g.Asked(34.40);
    CHECK(g.Waits(34.90, -1000.0));
    CHECK_FALSE(g.Waits(34.40 + prompt::kHintWait, -1000.0));
    // The next question of the interaction does not wait.
    g.Asked(38.70);
    CHECK_FALSE(g.Waits(38.75, -1000.0));
}

TEST_CASE("prompt: a hint said just before the first question needs no wait, and a new interaction waits again, 2026-11-04")
{
    // The danger totem on the pad (research/sessions/s43-only.log): the hint drawn at 02:56:09.43
    // and said at 09.70, the stick read at 09.85.
    prompt::HintFirst h;
    h.Interaction(true);
    h.Asked(9.70);
    CHECK_FALSE(h.Waits(9.75, 9.696));
    // Out of the interaction and into the next one: its first question waits again.
    h.Interaction(false);
    CHECK_FALSE(h.Waits(20.0, 9.696));
    h.Interaction(true);
    h.Asked(30.0);
    CHECK(h.Waits(30.1, 9.696));
}

TEST_CASE("prompts: an idle an interaction comes back to after a step takes its input again, 2026-11-06")
{
    // The kitchen door of chapter 3 (research/interactions/analysis/BP_Bashdoordown__act3_11.txt): the
    // push's prompt is drawn at idle_0 and hidden at each barge, and the door takes the same push in
    // idle_1 and idle_2 with no prompt drawn; a player who pushes once waits there for ever.
    CHECK_FALSE(prompt::IdleAfterStep(L"bash_door_down_idle_0"));
    CHECK(prompt::IdleAfterStep(L"bash_door_down_idle_1"));
    CHECK(prompt::IdleAfterStep(L"bash_door_down_idle_2"));
    CHECK_FALSE(prompt::IdleAfterStep(L"bash_door_down_barge_0"));
    // The idles of chapters 1 and 2's items are no steps' (the reveal, a page, a grab).
    CHECK_FALSE(prompt::IdleAfterStep(L"reveal_idle"));
    CHECK_FALSE(prompt::IdleAfterStep(L"page1_idle"));
    CHECK_FALSE(prompt::IdleAfterStep(L"grab_idle"));
    CHECK_FALSE(prompt::IdleAfterStep(L"_idle_1"));
    CHECK_FALSE(prompt::IdleAfterStep(L"door_idle_1a"));
    CHECK_FALSE(prompt::IdleAfterStep(L"door_idle_"));
}

TEST_CASE("an anim graph's state machines are asked by the index the game's own call takes, 2026-11-06")
{
    // GetCurrentStateName reads node Num - 1 - index of the graph's class as a state machine without
    // asking. A graph of five nodes whose second and fifth are machines: indices 3 and 0, nothing else.
    CHECK(prompt::MachineIndices({false, true, false, false, true}) == std::vector<int>{3, 0});
    // No machine, no index: 0 to 3 asked of every graph faulted 128 times in the session of 2026-11-06.
    CHECK(prompt::MachineIndices({false, false, false, false}).empty());
    CHECK(prompt::MachineIndices({}).empty());
    CHECK(prompt::MachineIndices({true}) == std::vector<int>{0});
}

TEST_CASE("a stage on a clock has run out only while nothing is in hand, 2026-11-06")
{
    // Jessica's phone in the cabin (research/sessions/s47-full.log): its stage gave 5 s from 22:28:19.75;
    // Mike reached for it, took hold at 22:28:21.7 and the interaction wound down until 22:28:24.6, its
    // own count going on.
    CHECK_FALSE(prompt::StageRanOut(4.9, 5.0, false, false));
    CHECK_FALSE(prompt::StageRanOut(5.2, 5.0, true, false));
    CHECK_FALSE(prompt::StageRanOut(5.2, 5.0, false, true));
    // Past its seconds with nothing taken hold of: the clock ran out.
    CHECK(prompt::StageRanOut(5.01, 5.0, false, false));
    // A stage with no clock never runs out.
    CHECK_FALSE(prompt::StageRanOut(30.0, 0.0, false, false));
}
