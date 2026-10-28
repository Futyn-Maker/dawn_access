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
