// features/PromptRules: when a prompt's question is new and when it is said, which lines of the
// action bar are said, and the blips of the phone's unlock swipe.
#include "Support.hpp"
#include "features/PromptRules.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
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

TEST_CASE("a stage on a clock has run out where a graph took the timeout from a waiting state, 2026-11-06")
{
    // Jessica's phone (research/sessions/s47-full.log): its 5 s passed as Mike held it, the graph in
    // ik_animation, which takes no timeout; it went on as it would have.
    CHECK_FALSE(prompt::TimeoutTaken({L"ik_animation"}, {L"SingleFrame_LastFrameTransform"}));
    // The death machine's lever (BP_Deathmachinelever__act4_1.txt): R2 held, deciding, in grab_idle
    // as its 30 s pass; the graph goes to grab_timeout_end: nobody is spared, and time is up.
    CHECK(prompt::TimeoutTaken({L"grab_idle"}, {L"grab_timeout_end"}));
    // Before the grab: reach_idle to reach_timeout_end.
    CHECK(prompt::TimeoutTaken({L"reach_idle", L"Locomotion"}, {L"reach_timeout_end", L"Locomotion"}));
    // Emily held over the ledge: GrabLoop to emily_pullsupself.
    CHECK(prompt::TimeoutTaken({L"GrabLoop"}, {L"emily_pullsupself"}));
    // A waiting state the graph has not left yet: not yet.
    CHECK_FALSE(prompt::TimeoutTaken({L"lifted_bolt_idle"}, {L"lifted_bolt_idle"}));
    CHECK(prompt::WaitingState(L"lifted_bolt_idle"));
    CHECK_FALSE(prompt::WaitingState(L"reach_to_grab"));
}

TEST_CASE("the death machine's lever says its sides in the game's names, chapter 4")
{
    // act4_1's BP_Deathmachinelever: pulled left, its graph sets ChrisMovedLeverAsh; right,
    // ChrisMovedLeverJosh (research/interactions/analysis/BP_Deathmachinelever__act4_1.txt). The names
    // are the characters' own (DA_CharacterMeta_Ashley and _Josh: LocalisedName).
    const auto sides = prompt::SidesOf(L"BP_Deathmachinelever_C");
    REQUIRE(sides.has_value());
    CHECK(sides->left == L"SMG013_66601");
    CHECK(sides->right == L"SMG013_66604");
    CHECK(sides->state == L"grab_idle");
    CHECK_FALSE(prompt::SidesOf(L"BP_Pullliftlever_C").has_value());
}

TEST_CASE("prompts: a staged shimmy's two-way input names the way on: act4_3's ledge, 2026-11-07")
{
    // research/sessions/s55-full.log: "the left stick left and right"; holding right took Matt and Emily on.
    CHECK(prompt::LinearForward(L"IA_InteractionMovement_LeftRight", false) == L"IA_InteractionMovement_Right");
    CHECK(prompt::LinearForward(L"IA_InteractionMovement_LeftRight", true) == L"IA_InteractionMovement_Left");
    CHECK(prompt::LinearForward(L"IA_InteractionMovement_UpDown", false) == L"IA_InteractionMovement_Up");
    CHECK(prompt::LinearForward(L"IA_InteractionMovement_UpDown", true) == L"IA_InteractionMovement_Down");
    // A one-way input's prompt names its way already.
    CHECK(prompt::LinearForward(L"IA_InteractionMovement_Right", false).empty());
    CHECK(prompt::LinearForward(L"", false).empty());
    CHECK(prompt::LinearForward(L"LeftRight", false).empty());
}

TEST_CASE("prompts: a page turned by a swipe names the way on by the swipe's own one-way action: Hannah's diary, 2026-11-08")
{
    // research/sessions/s58-whole.log, 11:19:46: the diary's stage 2 on IA_SwipeLeftRight, inverted;
    // the one-way prompts the game drew were IA_Swipe_Left and IA_Swipe_Right ("left stick left").
    // Built as IA_SwipeLeft, the action named no key and the way on was never said.
    CHECK(prompt::LinearForward(L"IA_SwipeLeftRight", true) == L"IA_Swipe_Left");
    CHECK(prompt::LinearForward(L"IA_SwipeLeftRight", false) == L"IA_Swipe_Right");
    CHECK(prompt::LinearForward(L"IA_SwipeUpDown", false) == L"IA_Swipe_Up");
}

TEST_CASE("prompts: the tower radio's dial keeps its place in a global state, the station at 3 up the scale, chapter 6")
{
    // act5_2's BP_Radio (research/mechanics/json_int/auto/bp_radio_b422d0.json): its stages keep the
    // needle's place in RadioDialPosition; MC_Gameplay binds IA_RotateAxisGesture's positive half and
    // IA_RotateRightGesture to the same keys (a clockwise half circle, D).
    const auto dial = prompt::DialOf(L"BP_Radio_C");
    REQUIRE(dial);
    CHECK(std::wstring(dial->state) == L"RadioDialPosition");
    CHECK(dial->station == 3);
    CHECK(std::wstring(dial->turnAction) == L"IA_RotateAxisGesture");
    CHECK(std::wstring(dial->forward) == L"IA_RotateRightGesture");
    CHECK_FALSE(prompt::DialOf(L"BP_Deathmachinelever_C"));
    CHECK_FALSE(prompt::DialOf(L""));
}

TEST_CASE("prompts: a staged climb's steps sound as a pitch that rises on and falls back, the tower's ladder, 2026-11-08")
{
    // act5_2's BP_Tower_ladder: 20 steps (MaxAnimationStage), the left stick up climbs, down climbs back.
    prompt::LinearProgress climb;
    CHECK_FALSE(climb.Update(0, 20)); // the stage's first reading: no step
    const auto one = climb.Update(1, 20);
    REQUIRE(one);
    CHECK(*one == doctest::Approx(0.05));
    CHECK_FALSE(climb.Update(1, 20)); // the same step: nothing
    const auto two = climb.Update(2, 20);
    REQUIRE(two);
    CHECK(*two > *one);
    const auto back = climb.Update(1, 20);
    REQUIRE(back);
    CHECK(*back < *two);
    const auto top = climb.Update(20, 20);
    REQUIRE(top);
    CHECK(*top == doctest::Approx(1.0));
    // A new stage starts over; no step known, or no last, sounds nothing.
    climb.Reset();
    CHECK_FALSE(climb.Update(5, 20));
    CHECK_FALSE(climb.Update(-1, 20));
    CHECK_FALSE(climb.Update(6, 0));
}

namespace
{
    // A compiled script as the engine keeps it in memory: EX_CallMath (0x68) and the function's
    // address, the comparison's operands, EX_EndFunctionParms (0x16).
    void Compare(std::vector<uint8_t>& script, const void* property, int number, bool memberFirst, bool byteConst = false)
    {
        const auto put = [&](const void* data, size_t size)
        {
            const auto* bytes = static_cast<const uint8_t*>(data);
            script.insert(script.end(), bytes, bytes + size);
        };
        const void* function = reinterpret_cast<const void*>(uintptr_t{0x7FF6'1234'5678});
        script.push_back(0x68);
        put(&function, sizeof function);
        const auto member = [&]
        {
            script.push_back(0x01);
            put(&property, sizeof property);
        };
        const auto constant = [&]
        {
            if (byteConst)
            {
                script.push_back(0x2C);
                script.push_back(static_cast<uint8_t>(number));
                return;
            }
            script.push_back(0x1D);
            const int32_t n = number;
            put(&n, sizeof n);
        };
        if (memberFirst)
        {
            member();
            constant();
        }
        else
        {
            constant();
            member();
        }
        script.push_back(0x16);
    }
}

TEST_CASE("prompts: a staged ladder that keeps no last step ends where its graph compares the step: act6_1's hub ladder, 2026-11-08")
{
    // BP_Hubladder's stage clamps nothing (bClampAnimationStage off), so the stage said "its steps
    // unbounded" and no step sounded up the ladder (research/sessions/s59-full.log 15:47:41). Its graph
    // (ABP_Act6_1_groundfloorhub_climbladder_template) compares AnimationStage with 0 to 14 in
    // EqualEqual_IntInt, 14 taking it to Step13_to_End.
    int animationStage = 0; // stands for the member's property: only its address is compared
    int otherMember = 0;
    std::vector<uint8_t> script;
    for (int n = 0; n <= 14; ++n)
        Compare(script, &animationStage, n, true);
    // Another member compared with a larger number is no step.
    Compare(script, &otherMember, 99, true);
    auto last = prompt::LastComparedStep(script.data(), script.size(), &animationStage);
    REQUIRE(last.has_value());
    CHECK(*last == 14);
    // The number before the member, and a number kept in a byte, either side, count the same.
    const auto alone = [&](int number, bool memberFirst, bool byteConst) -> std::optional<int64_t>
    {
        std::vector<uint8_t> one;
        Compare(one, &otherMember, 99, true);
        Compare(one, &animationStage, number, memberFirst, byteConst);
        return prompt::LastComparedStep(one.data(), one.size(), &animationStage);
    };
    CHECK(alone(3, false, false) == std::optional<int64_t>{3});
    CHECK(alone(16, false, true) == std::optional<int64_t>{16});
    CHECK(alone(9, true, true) == std::optional<int64_t>{9});
    CHECK(alone(12, true, false) == std::optional<int64_t>{12});
    std::vector<uint8_t> swapped;
    Compare(swapped, &animationStage, 3, false);
    Compare(swapped, &animationStage, 16, false, true);
    Compare(swapped, &animationStage, 9, true, true);
    last = prompt::LastComparedStep(swapped.data(), swapped.size(), &animationStage);
    REQUIRE(last.has_value());
    CHECK(*last == 16);
    // A graph that never compares the step with a number ends nothing; nor does a script that only
    // names the member's address elsewhere (not as a member read).
    std::vector<uint8_t> none;
    Compare(none, &otherMember, 4, true);
    const void* bare = &animationStage;
    const auto* bytes = reinterpret_cast<const uint8_t*>(&bare);
    none.push_back(0x2B);
    none.insert(none.end(), bytes, bytes + sizeof bare);
    none.push_back(0x1D);
    none.insert(none.end(), {7, 0, 0, 0});
    CHECK_FALSE(prompt::LastComparedStep(none.data(), none.size(), &animationStage).has_value());
    CHECK_FALSE(prompt::LastComparedStep(nullptr, 0, &animationStage).has_value());
    // A math function whose address has 0x1D for its fourth byte, the member its first operand: the
    // bytes before the member read like a number (the address's top half, 498 here), and are not one.
    {
        std::vector<uint8_t> call;
        const uint64_t function = 0x000001F21D345678ull;
        const void* member = &animationStage;
        call.push_back(0x68);
        const auto* f = reinterpret_cast<const uint8_t*>(&function);
        call.insert(call.end(), f, f + sizeof function);
        call.push_back(0x01);
        const auto* m = reinterpret_cast<const uint8_t*>(&member);
        call.insert(call.end(), m, m + sizeof member);
        call.push_back(0x1D);
        call.insert(call.end(), {14, 0, 0, 0});
        call.push_back(0x16);
        const auto found = prompt::LastComparedStep(call.data(), call.size(), &animationStage);
        REQUIRE(found.has_value());
        CHECK(*found == 14);
    }
    // An assignment to the member (EX_Let, 0x0F, the member's address, its read, the number) is no
    // comparison.
    {
        std::vector<uint8_t> let;
        const void* member = &animationStage;
        const auto* m = reinterpret_cast<const uint8_t*>(&member);
        let.push_back(0x0F);
        let.insert(let.end(), m, m + sizeof member);
        let.push_back(0x01);
        let.insert(let.end(), m, m + sizeof member);
        let.push_back(0x1D);
        let.insert(let.end(), {40, 0, 0, 0});
        CHECK_FALSE(prompt::LastComparedStep(let.data(), let.size(), &animationStage).has_value());
    }
    // The graph ends a stage that clamps nothing, whatever MaxAnimationStage names; a clamped stage
    // ends at its own last; a stage whose graph compares nothing keeps the last it names.
    CHECK(prompt::LastStep(false, 0, 14) == std::optional<int64_t>{14});
    CHECK(prompt::LastStep(false, 4, 14) == std::optional<int64_t>{14});
    CHECK(prompt::LastStep(true, 4, 14) == std::optional<int64_t>{4});
    CHECK(prompt::LastStep(true, 3, std::nullopt) == std::optional<int64_t>{3});
    CHECK(prompt::LastStep(false, 4, std::nullopt) == std::optional<int64_t>{4});
    CHECK_FALSE(prompt::LastStep(false, 0, std::nullopt).has_value());
    CHECK_FALSE(prompt::LastStep(false, 0, 0).has_value());
    // Each step up the ladder then sounds, rising to the top.
    prompt::LinearProgress progress;
    CHECK_FALSE(progress.Update(1, 14).has_value());
    const auto second = progress.Update(2, 14);
    REQUIRE(second.has_value());
    CHECK(*second == doctest::Approx(2.0 / 14.0));
    CHECK(*progress.Update(14, 14) == doctest::Approx(1.0));
}

TEST_CASE("prompts: a staged stage's prompt drawn again at each step is said once: act6_1's ladders, 2026-11-08")
{
    // The hub ladder (BP_Hubladder, stage 1): the prompt came up at 15:47:42.458 and again at
    // 15:47:43.033 after the first step, the way on said between them; Andrey went over to the
    // keyboard at 15:47:52 ("W, S") and back to the pad at 15:48:30.
    prompt::StagePrompt last;
    const int64_t ladder = 1 * 1024 + 0;
    const std::wstring pad = L"\u043b\u0435\u0432\u044b\u0439 \u0441\u0442\u0438\u043a \u0432\u0432\u0435\u0440\u0445 \u0438 \u0432\u043d\u0438\u0437";
    const std::wstring keys = L"W, S";
    const std::wstring action = L"IA_InteractionMovement_UpDown";
    CHECK(prompt::SayAtStep(last, ladder, pad, action));
    CHECK_FALSE(prompt::SayAtStep(last, ladder, pad, action));
    CHECK(prompt::SayAtStep(last, ladder, keys, action));
    CHECK_FALSE(prompt::SayAtStep(last, ladder, keys, action));
    CHECK(prompt::SayAtStep(last, ladder, pad, action));
    CHECK_FALSE(prompt::SayAtStep(last, ladder, pad, action));
    // The broken ladder by the miner's accident (BP_Climbladder, held up to the top at 15:53:05 to
    // 15:53:19): fourteen prompts, said once.
    const int64_t broken = 2 * 1024 + 0;
    const std::wstring up = L"\u043b\u0435\u0432\u044b\u0439 \u0441\u0442\u0438\u043a \u0432\u0432\u0435\u0440\u0445";
    int said = 0;
    for (int step = 0; step < 14; ++step)
        said += prompt::SayAtStep(last, broken, up, L"IA_InteractionMovement_Up") ? 1 : 0;
    CHECK(said == 1);
    // Out of a staged stage whose steps sound every prompt is said, the same words or not.
    CHECK(prompt::SayAtStep(last, -1, up, L"IA_InteractionMovement_Up"));
    CHECK(prompt::SayAtStep(last, -1, up, L"IA_InteractionMovement_Up"));
    // Another stage, or the same ladder used again, says its prompt again.
    CHECK(prompt::SayAtStep(last, broken, up, L"IA_InteractionMovement_Up"));
    CHECK(prompt::SayAtStep(last, 3 * 1024 + 0, up, L"IA_InteractionMovement_Up"));
}

TEST_CASE("prompts: an activity's input stage that does not wait is a quick-time event's: Emily's swings and chapter 8's bolt, 2026-11-08")
{
    using W = prompt::InputWindow;
    // A_cut_the_cable_emilyhanging: EmilyHangingIdle1 loops at its end marker and waits for the push;
    // swing1_QTE to swing3_QTE go on at theirs (0.83, 0.67 and 0.60 s).
    CHECK(prompt::WindowOf(prompt::InputStage{1, false, 5.0, false}) == W::Waits);
    CHECK(prompt::WindowOf(prompt::InputStage{0, false, 5.0, false}) == W::Moment);
    CHECK(prompt::WindowOf(prompt::InputStage{2, false, 5.0, false}) == W::Waits);
    // Chapter 4's shimmy steps go on at their end marker into an idle that takes the same push: no
    // moment to catch (act3_2 act3_2_minechase_shimmy_step1 to step7, 0.9 to 1.3 s, to TempIdle..6).
    CHECK(prompt::WindowOf(prompt::InputStage{0, false, 5.0, true}) == W::Waits);
    // act6_3's BoltDoorInteraction: bShouldTimeOut, 3 s; act7_2's stand_idle keeps the stage's 5 s.
    CHECK(prompt::WindowOf(prompt::InputStage{1, true, 3.0, false}) == W::Clock);
    CHECK(prompt::WindowOf(prompt::InputStage{0, true, 5.0, true}) == W::Clock);
    // The stage's own record as its prompt goes: the push taken, else its time up.
    CHECK(prompt::EndOf(true, false, false, true) == prompt::InputEnd::Taken);
    CHECK(prompt::EndOf(true, true, false, true) == prompt::InputEnd::Taken);
    CHECK(prompt::EndOf(false, true, false, true) == prompt::InputEnd::TimedOut);
    CHECK(prompt::EndOf(false, false, false, true) == prompt::InputEnd::Closed);
    // A swing played again after its push was taken once keeps the push's outcome: it goes on as if
    // pushed, and nothing was missed.
    CHECK(prompt::EndOf(false, false, true, true) == prompt::InputEnd::Taken);
    CHECK(prompt::EndOf(false, true, true, true) == prompt::InputEnd::TimedOut);
    // A window whose end names no stage (act20_3's pages) has no verdict; its clock, if it had one,
    // would still be a clock.
    CHECK(prompt::EndOf(false, false, false, false) == prompt::InputEnd::Over);
    CHECK(prompt::EndOf(false, true, false, false) == prompt::InputEnd::TimedOut);
    CHECK(prompt::EndOf(true, false, false, false) == prompt::InputEnd::Taken);
    // The swings of 15:35:36 to 15:36:28: windows gone without the push, the push taken in the fourth
    // swing1 window and the first swing2 window, then five swing3 windows missed until the stick was
    // held, taken as the sixth came up.
    struct Window
    {
        double up, gone;
        bool taken;
    };
    const Window windows[] = {{36.597, 37.437, false}, {41.583, 42.432, false}, {46.582, 47.432, false}, {51.607, 52.179, true},
                              {57.517, 58.140, true},  {63.368, 63.978, false}, {68.371, 68.989, false}, {73.390, 74.003, false},
                              {78.383, 78.991, false}, {83.369, 83.968, false}, {88.348, 88.349, true}};
    int taken = 0;
    int missed = 0;
    for (const auto& w : windows)
    {
        CHECK(w.gone > w.up);
        (prompt::EndOf(w.taken, false, false, true) == prompt::InputEnd::Taken ? taken : missed) += 1;
    }
    CHECK(taken == 3);
    CHECK(missed == 8);
}

TEST_CASE("prompts: a move of the stick drawn beside the grab is made with the grab held: the flare, 2026-11-08")
{
    // act6_3's flare: "левый стик вправо, R2" [IA_Swipe_Right, IA_GrabInteraction].
    CHECK(std::wstring(prompt::HeldMoveHelp(L"IA_Swipe_Right, IA_GrabInteraction")) == L"help.swipe.held");
    // Chapter 3's lantern: a half circle beside the grab.
    CHECK(std::wstring(prompt::HeldMoveHelp(L"IA_RotateRightGesture, IA_GrabInteraction")) == L"help.gesture.held");
    // A swipe alone (the zippo's first prompt), the grab alone, and the hub ladder's stick: none.
    CHECK(prompt::HeldMoveHelp(L"IA_Swipe_Right") == nullptr);
    CHECK(prompt::HeldMoveHelp(L"IA_GrabInteraction") == nullptr);
    CHECK(prompt::HeldMoveHelp(L"IA_InteractionMovement_UpDown") == nullptr);
}

TEST_CASE("prompts: a move drawn beside the grab is said with the grab held, the grab first, 2026-11-08")
{
    // act6_3's flare draws the swipe first and the grab beside it.
    auto pair = prompt::HeldMoveOf({L"IA_Swipe_Right", L"IA_GrabInteraction"});
    REQUIRE(pair);
    CHECK(pair->grab == 1);
    CHECK(pair->move == 0);
    // act5_2's radio draws the grab first and the half circle beside it.
    pair = prompt::HeldMoveOf({L"IA_GrabInteraction", L"IA_RotateAxisGesture"});
    REQUIRE(pair);
    CHECK(pair->grab == 0);
    CHECK(pair->move == 1);
    // The doors' and lids' pushes and pulls, once the grab is held ("move_towards", "move_left").
    CHECK(prompt::HeldMoveOf({L"IA_InteractionMovement_Down", L"IA_GrabInteraction"}));
    CHECK(prompt::HeldMoveOf({L"IA_InteractionMovement_LeftRight", L"IA_GrabInteraction"}));
    CHECK(prompt::HeldMoveOf({L"IA_RotateLeftGestureOppositeKeyboard", L"IA_GrabInteraction"}));
    // The grab alone ("hold"), a move alone (the journal's pages), the grab beside the way back or
    // beside a press that is no move, and a third action: none.
    CHECK_FALSE(prompt::HeldMoveOf({L"IA_GrabInteraction"}));
    CHECK_FALSE(prompt::HeldMoveOf({L"IA_SwipeLeftRight"}));
    CHECK_FALSE(prompt::HeldMoveOf({L"IA_GrabInteraction", L"IA_BackoutInteraction"}));
    CHECK_FALSE(prompt::HeldMoveOf({L"IA_GrabInteraction", L"IA_Continue"}));
    CHECK_FALSE(prompt::HeldMoveOf({L"IA_Swipe_Right", L"IA_GrabInteraction", L"IA_BackoutInteraction"}));
    CHECK_FALSE(prompt::HeldMoveOf({L"IA_Swipe_Right", L"IA_Swipe_Left"}));
}

TEST_CASE("prompts: the grab is held as the stage holds it, the setting where the stage is not known")
{
    // The flare's stage insists on holding (bStageShouldAlwaysUseHoldGrab): bUsingToggleGrab false
    // whatever the setting.
    CHECK(prompt::GrabHeld(false, true));
    CHECK(prompt::GrabHeld(false, false));
    // A door's stage with the player's "toggle grab" setting on: the grab is on between two presses.
    CHECK_FALSE(prompt::GrabHeld(true, true));
    // No stage drew the widget: the setting decides.
    CHECK(prompt::GrabHeld(std::nullopt, false));
    CHECK_FALSE(prompt::GrabHeld(std::nullopt, true));
}

TEST_CASE("prompts: the snow globe's hidden shake is named by what takes it, chapter 7")
{
    // The keyboard: W or S.
    CHECK(std::wstring(prompt::ShakeHelp(false, false, false)) == L"help.shake");
    CHECK(std::wstring(prompt::ShakeHelp(false, true, true)) == L"help.shake");
    // Another pad: its left stick up or down.
    CHECK(std::wstring(prompt::ShakeHelp(true, false, false)) == L"help.shake.pad");
    CHECK(std::wstring(prompt::ShakeHelp(true, false, true)) == L"help.shake.pad");
    // A PlayStation pad: shaken, with the game's Motion Sensor Function on; with it off, its stick
    // gives nothing, and the line says the keyboard's keys or the setting.
    CHECK(std::wstring(prompt::ShakeHelp(true, true, true)) == L"help.shake.ps");
    CHECK(std::wstring(prompt::ShakeHelp(true, true, false)) == L"help.shake.ps.off");
}

TEST_CASE("prompts: a staged stage standing past its first step names its way back: Jack's journal, 2026-11-08")
{
    // BP_Readjournal opens at its second page (InitialAnimationStage 1): its foreword lies a push right
    // back, with a clue of its own.
    CHECK(prompt::NamesWayBack(1, 1));
    // A climb, a shimmy, a book opened at its first page: only the way on.
    CHECK_FALSE(prompt::NamesWayBack(0, 0));
    CHECK_FALSE(prompt::NamesWayBack(-1, 0));
    // The graph's step not readable: the stage's own first step tells.
    CHECK(prompt::NamesWayBack(-1, 1));
    // The graph's step read wins: turned back to the foreword before the prompt came, nothing lies
    // behind; turned on from a first page, the way back leads somewhere.
    CHECK_FALSE(prompt::NamesWayBack(0, 1));
    CHECK(prompt::NamesWayBack(2, 0));
    // The way back is the way on turned round.
    CHECK(prompt::LinearForward(L"IA_SwipeLeftRight", true) == L"IA_Swipe_Left");
    CHECK(prompt::LinearForward(L"IA_SwipeLeftRight", false) == L"IA_Swipe_Right");
}

TEST_CASE("prompts: the bar's prompts shown again with a page being read are said only where they are new")
{
    // Jack's journal (research/sessions/s61-play.log): the page said at 100.0, the bar's scroll and its
    // key at 101.2; Ashley's comment hides the page and the bar, both shown again at 104.5.
    const std::map<std::wstring, double> saidAt{{L"Прокрутка: правый стик", 101.2}, {L"Скрыть текст: треугольник", 101.2}, {L"Назад: круг", 60.0}};
    const double now = 104.6;
    const double sincePage = now - 100.0 + 1.0;
    // Said since the page: still to be said behind it, or said with it.
    CHECK(prompt::NotSaidWithin({L"Прокрутка: правый стик", L"Скрыть текст: треугольник"}, saidAt, now, sincePage).empty());
    // Said before the page, or never: new, and said.
    const auto fresh = prompt::NotSaidWithin({L"Прокрутка: правый стик", L"Назад: круг", L"Дальше: левый стик влево"}, saidAt, now, sincePage);
    REQUIRE(fresh.size() == 2);
    CHECK(fresh[0] == L"Назад: круг");
    CHECK(fresh[1] == L"Дальше: левый стик влево");
}

TEST_CASE("a staged stage's own prompt is said once while it stands, a step's own whenever it changes: the office desk, 2026-11-09")
{
    // research/sessions/s63-play.log 00:23:22 to 00:24:56: the pages of BP_Officedesk (stage 3 on
    // IA_SwipeLeftRight), its first page drawing the way on alone, its last the way back alone, and
    // the stage's own prompt drawn again half a second after each end (its UIIdleTime).
    const std::wstring main = L"IA_SwipeLeftRight";
    const std::wstring both = L"левый стик влево и вправо";
    const std::wstring right = L"левый стик вправо";
    const std::wstring left = L"левый стик влево";
    struct Drawn
    {
        std::wstring text;
        std::wstring actions;
    };
    const std::vector<Drawn> desk{
        {both, main},               // the stage begins
        {right, L"IA_Swipe_Right"}, // page 0
        {both, main},               // page 1
        {both, main},               // page 2
        {left, L"IA_Swipe_Left"},   // page 3
        {both, main},               // its idle: the stage's own again
        {both, main},               // back to page 2
        {both, main},               // page 1
        {right, L"IA_Swipe_Right"}, // page 0
        {both, main},               // its idle
    };
    prompt::StagePrompt last;
    std::vector<std::wstring> said;
    for (const auto& d : desk)
    {
        if (prompt::SayAtStep(last, 3, d.text, d.actions, main)) said.push_back(d.text);
    }
    CHECK(said == std::vector<std::wstring>{both, right, left, right});
    // Another device in hand: the stage's own prompt in new words is said.
    CHECK(prompt::SayAtStep(last, 3, L"A и D", main, main));
    // Another stage: said afresh.
    CHECK(prompt::SayAtStep(last, 4, both, main, main));
    // No staged stage: every prompt is said.
    CHECK(prompt::SayAtStep(last, -1, both, main, main));
    CHECK(prompt::SayAtStep(last, -1, both, main, main));
}

TEST_CASE("the bar's lines are said once while a staged stage stands, though it empties at every step")
{
    prompt::Bar bar;
    const std::wstring back = L"Назад: круг";
    const std::wstring hide = L"Скрыть расшифровку: квадрат";
    const auto settled = [&](double t, const std::vector<std::wstring>& lines, int64_t scope)
    {
        bar.Update(t, false, lines, scope);
        return bar.Update(t + prompt::kBarStable + 0.01, false, lines, scope);
    };
    CHECK(settled(0.0, {back}, 3) == std::vector<std::wstring>{back});
    CHECK(settled(1.0, {}, 3).empty());
    CHECK(settled(2.0, {back}, 3).empty());
    CHECK(settled(3.0, {hide}, 3) == std::vector<std::wstring>{hide});
    CHECK(settled(4.0, {}, 3).empty());
    CHECK(settled(5.0, {back}, 3).empty());
    CHECK(settled(6.0, {hide}, 3).empty());
    // Another stage, or none: a line that comes back is said again.
    CHECK(settled(7.0, {}, 4).empty());
    CHECK(settled(8.0, {back}, 4) == std::vector<std::wstring>{back});
    CHECK(settled(9.0, {}, -1).empty());
    CHECK(settled(10.0, {back}, -1) == std::vector<std::wstring>{back});
    CHECK(settled(11.0, {}, -1).empty());
    CHECK(settled(12.0, {back}, -1) == std::vector<std::wstring>{back});
}

TEST_CASE("a staged stage's way on waits for the step's own prompt, and is not said where the game names it alone: the office desk, 2026-11-09")
{
    using prompt::WayOn;
    const std::wstring main = L"IA_SwipeLeftRight";
    const std::wstring forward = L"IA_Swipe_Right";
    // The desk: its own prompt 0.16 s in, the way said 1.04 s in by the old rule, its first page's
    // prompt naming the way on alone 2.3 s in.
    CHECK(prompt::WayOnNow(0.5, true, main, main, forward, true) == WayOn::Wait);
    CHECK(prompt::WayOnNow(1.04, true, main, main, forward, true) == WayOn::Wait);
    CHECK(prompt::WayOnNow(2.3, true, forward, main, forward, true) == WayOn::Skip);
    // A step's own prompt that names something else: the way on is said.
    CHECK(prompt::WayOnNow(2.3, true, L"IA_GrabInteraction", main, forward, true, L"IA_Swipe_Left") == WayOn::Say);
    // One that names the way back alone stands at the last step, where there is no way on.
    CHECK(prompt::WayOnNow(2.3, true, L"IA_Swipe_Left", main, forward, true, L"IA_Swipe_Left") == WayOn::Skip);
    // No step's prompt by kStepPromptWait: said.
    CHECK(prompt::WayOnNow(prompt::kStepPromptWait, true, main, main, forward, true) == WayOn::Say);
    // A stage that draws no prompts at its steps: after its own, as before.
    CHECK(prompt::WayOnNow(0.5, true, main, main, forward, false) == WayOn::Wait);
    CHECK(prompt::WayOnNow(1.04, true, main, main, forward, false) == WayOn::Say);
    CHECK(prompt::WayOnNow(1.5, false, L"", main, forward, false) == WayOn::Wait);
    CHECK(prompt::WayOnNow(2.0, false, L"", main, forward, false) == WayOn::Wait);
    // Nothing drawn by kPromptWait: said all the same.
    CHECK(prompt::WayOnNow(prompt::kPromptWait, false, L"", main, forward, false) == WayOn::Say);
    CHECK(prompt::WayOnNow(prompt::kPromptWait, false, L"", main, forward, true) == WayOn::Say);
}

TEST_CASE("a staged climb's way on waits for the game's own prompt: the radio tower's ladders, 2026-11-12")
{
    // research/sessions/s72-play.log: the ladders' stage (BP_Interaction_Stage_Linear_Staged on
    // IA_InteractionMovement_UpDown, going on with IA_InteractionMovement_Up, drawing prompts at its
    // steps) began as the ladder was taken; the game drew "левый стик вверх и вниз" 2.97 s in on the
    // first, 3.13 and 3.20 s in on the second and third, and the way on was said at 3.11, 3.12 and
    // 3.19 s: after the game's prompt once, before it twice.
    using prompt::WayOn;
    const std::wstring main = L"IA_InteractionMovement_UpDown";
    const std::wstring forward = L"IA_InteractionMovement_Up";
    const std::wstring back = L"IA_InteractionMovement_Down";
    // The second ladder, 3.12 s in, nothing drawn yet: the way waits.
    CHECK(prompt::WayOnNow(3.12, false, L"", main, forward, true, back) == WayOn::Wait);
    // The game's prompt drawn at 3.13 s: the way follows it.
    CHECK(prompt::WayOnNow(3.14, true, main, main, forward, true, back) == WayOn::Say);
    // The first ladder, its prompt drawn at 2.97 s: the way waits for a step's own prompt to 3 s, then follows.
    CHECK(prompt::WayOnNow(2.98, true, main, main, forward, true, back) == WayOn::Wait);
    CHECK(prompt::WayOnNow(3.11, true, main, main, forward, true, back) == WayOn::Say);
    // A prompt read a moment before the stage is noticed (the stage looked at every sixth frame) is the
    // stage's; one read well before is another's.
    CHECK(prompt::PromptedSince(100.0, 100.0));
    CHECK(prompt::PromptedSince(99.9, 100.0));
    CHECK(prompt::PromptedSince(101.0, 100.0));
    CHECK_FALSE(prompt::PromptedSince(99.5, 100.0));
}

TEST_CASE("while a staged stage stands the bar says a line its action has new words for: the transcript's key toggled")
{
    prompt::Bar bar;
    const std::wstring hide = L"Скрыть расшифровку: квадрат", show = L"Показать расшифровку: квадрат", back = L"Назад: круг";
    const auto settled = [&](double t, const std::vector<std::wstring>& lines, const std::vector<std::wstring>& actions)
    {
        bar.Update(t, false, lines, 3, actions);
        return bar.Update(t + prompt::kBarStable + 0.01, false, lines, 3, actions);
    };
    CHECK(settled(0.0, {back}, {L"IA_BackoutInteraction"}) == std::vector<std::wstring>{back});
    CHECK(settled(1.0, {hide, back}, {L"IA_Read", L"IA_BackoutInteraction"}) == std::vector<std::wstring>{hide});
    CHECK(settled(2.0, {show, back}, {L"IA_Read", L"IA_BackoutInteraction"}) == std::vector<std::wstring>{show});
    CHECK(settled(3.0, {hide, back}, {L"IA_Read", L"IA_BackoutInteraction"}) == std::vector<std::wstring>{hide});
    // A step: the bar empties and fills with the same words; nothing new.
    CHECK(settled(4.0, {}, {}).empty());
    CHECK(settled(5.0, {back}, {L"IA_BackoutInteraction"}).empty());
    CHECK(settled(6.0, {hide, back}, {L"IA_Read", L"IA_BackoutInteraction"}).empty());
}

TEST_CASE("a step's prompts wait for the page only after a turn, and not long: the transcript's own key holds nothing")
{
    // A turn of the desk's pages: the step changed, the page hidden as it turns.
    CHECK(prompt::HoldsForPage(true, true, true, 0.3));
    CHECK_FALSE(prompt::HoldsForPage(true, true, true, prompt::kPageHoldMost));
    // The transcript hidden by its key: the page faded, the step the same.
    CHECK_FALSE(prompt::HoldsForPage(true, true, false, 0.3));
    // No staged stage, or no page coming.
    CHECK_FALSE(prompt::HoldsForPage(false, true, true, 0.3));
    CHECK_FALSE(prompt::HoldsForPage(true, false, true, 0.3));
}

TEST_CASE("prompt: a stage on a clock sounds its cue only once it asks; the prologue's shack door asks nothing, 2026-11-10")
{
    // BP_ShackScare's LookThroughDoorStage (research/sessions/s67-play.log, 19:10:24.912 to 19:10:32.941):
    // the push's prompt sent away as the stage began, 8 s with nothing drawn, then Beth's scare. The cue
    // had sounded at its start.
    prompt::TimedCue cue;
    CHECK_FALSE(cue.Update(-1, true)); // the push before it: no stage on a clock
    for (int poll = 0; poll < 48; ++poll)
        CHECK_FALSE(cue.Update(1, false));
    CHECK_FALSE(cue.Asked(1)); // its clock running out is no time's up
    CHECK_FALSE(cue.Update(-1, false));
    // Jessica's phone in chapter 3 (5 s): the question drawn a moment after the stage began: the cue then,
    // once, and its running out is said.
    prompt::TimedCue phone;
    CHECK_FALSE(phone.Update(0, false));
    CHECK(phone.Update(0, true));
    CHECK_FALSE(phone.Update(0, true));
    CHECK_FALSE(phone.Update(0, false));
    CHECK(phone.Asked(0));
    // The psycho door's bolt: its question already drawn as the stage on a clock begins: the cue at once.
    prompt::TimedCue bolt;
    CHECK(bolt.Update(2, true));
    CHECK(bolt.Asked(2));
    // The next stage on a clock asks anew.
    CHECK_FALSE(bolt.Update(3, false));
    CHECK_FALSE(bolt.Asked(2));
    CHECK(bolt.Update(3, true));
}

TEST_CASE("prompts: an interaction's prompt asks where the game draws it on a layer of its own")
{
    // s71 21:55:50: the death machine's R2 (WBP_Interaction_C), read by the prompt reader on its screen
    // layer, in no widget tree: asked for the tree alone, its stage's 30 s never sounded the Timer cue,
    // nor did Jessica's phone (21:14:48) or the bolt (23:44:45), and the clock run out went unsaid.
    prompt::TimedCue lever;
    CHECK_FALSE(lever.Update(0, prompt::Asking(false, false, false, false, false))); // the stage begins, nothing drawn
    CHECK(lever.Update(0, prompt::Asking(false, false, true, false, true)));         // R2 drawn: the cue
    CHECK(lever.Asked(0));                                                           // its 30 s run out is said
    // Drawn in the viewport's tree: asks as well.
    CHECK(prompt::Asking(false, false, true, true, false));
    // Sent away, or without words, or a glint's, or drawn nowhere: asks nothing.
    CHECK_FALSE(prompt::Asking(false, true, true, false, true));
    CHECK_FALSE(prompt::Asking(false, false, false, false, true));
    CHECK_FALSE(prompt::Asking(true, false, true, false, true));
    CHECK_FALSE(prompt::Asking(false, false, true, false, false));
}

namespace
{
    // act3_2 A_aim_at_killer's mine shimmy as the activity holds it (research/mechanics/input_stages.tsv).
    std::vector<prompt::RunStage> MineShimmy()
    {
        const std::wstring right = L"IA_InteractionMovement_Right";
        std::vector<prompt::RunStage> stages;
        for (int i = 1; i <= 7; ++i)
            stages.push_back({L"act3_2_minechase_shimmy_step" + std::to_wstring(i), right, L"act3_2_minechase_shimmy_step" + std::to_wstring(i + 1), false});
        stages.push_back({L"TempIdle", right, L"act3_2_minechase_shimmy_step2", true});
        for (int i = 1; i <= 6; ++i)
            stages.push_back({L"TempIdle" + std::to_wstring(i), right, L"act3_2_minechase_shimmy_step" + std::to_wstring(i + 2), true});
        // Elsewhere in the activity: a QTE's stage on another input, an aim stage.
        stages.push_back({L"aim_idle", L"IA_SequenceAim", L"aim_shoot", true});
        return stages;
    }
}

TEST_CASE("prompts: an activity's run of steps on one input says its prompt once and sounds each step")
{
    const auto stages = MineShimmy();
    const auto first = prompt::PlaceInRun(stages, L"act3_2_minechase_shimmy_step1");
    CHECK(first.head == L"act3_2_minechase_shimmy_step1");
    CHECK(first.steps == 7);
    CHECK(first.step == 0);
    CHECK_FALSE(first.idle);
    CHECK(prompt::PlaceInRun(stages, L"act3_2_minechase_shimmy_step4").step == 3);
    const auto idle = prompt::PlaceInRun(stages, L"TempIdle");
    CHECK(idle.idle);
    CHECK(idle.step == 0);
    CHECK(idle.head == L"act3_2_minechase_shimmy_step1");
    CHECK(prompt::PlaceInRun(stages, L"TempIdle6").step == 6); // it leads where the last step does
    CHECK(prompt::PlaceInRun(stages, L"aim_idle").head.empty());
    CHECK(prompt::PlaceInRun(stages, L"no such stage").head.empty());

    // s71 21:32:56 to 21:33:03: step 1's prompt, its stretch run out to TempIdle, the push held from
    // there through step 7. Said once; each step on a higher pitch, the last the highest.
    prompt::Run run;
    std::optional<double> level;
    const std::wstring push = L"левый стик вправо";
    CHECK(run.Prompt(prompt::PlaceInRun(stages, L"act3_2_minechase_shimmy_step1"), push, 56.78, level));
    CHECK_FALSE(level);
    CHECK_FALSE(run.Prompt(prompt::PlaceInRun(stages, L"TempIdle"), push, 57.92, level));
    CHECK_FALSE(level);
    std::vector<double> levels;
    double at = 58.28;
    for (int i = 2; i <= 7; ++i, at += 1.1)
    {
        CHECK_FALSE(run.Prompt(prompt::PlaceInRun(stages, L"act3_2_minechase_shimmy_step" + std::to_wstring(i)), push, at, level));
        REQUIRE(level);
        levels.push_back(*level);
    }
    CHECK(std::is_sorted(levels.begin(), levels.end()));
    CHECK(levels.front() > 0.0);
    CHECK(levels.back() == doctest::Approx(1.0));
    // An idle sounds no step, wherever it stands.
    CHECK_FALSE(run.Prompt(prompt::PlaceInRun(stages, L"TempIdle1"), push, at, level));
    CHECK_FALSE(level);
    // The prompt's words changed (another device in hand): said.
    CHECK(run.Prompt(prompt::PlaceInRun(stages, L"act3_2_minechase_shimmy_step7"), L"D", at, level));
    // The run begun anew after a pause (a checkpoint loaded): said again.
    CHECK(run.Prompt(prompt::PlaceInRun(stages, L"act3_2_minechase_shimmy_step1"), L"D", at + 30.0, level));
    // A stage in no run: said, as any prompt.
    CHECK(run.Prompt(prompt::RunPlace{}, push, at + 31.0, level));
    CHECK_FALSE(level);
}

TEST_CASE("prompts: chapter 8's tunnel is a run of two steps, each idle at its own")
{
    // act6_3 A_grabbed: step44 and step45 pause at their ends; AddativeIdle43 leads to step45,
    // AddativeIdle45 where step45 goes on.
    const std::wstring up = L"IA_InteractionMovement_Up";
    const std::vector<prompt::RunStage> stages{
        {L"act6_3_meetjack_throughtunnel_step44", up, L"act6_3_meetjack_throughtunnel_step45", false},
        {L"act6_3_meetjack_throughtunnel_step45", up, L"act6_3_meetjack_throughtunnel_step46", false},
        {L"AddativeIdle43", up, L"act6_3_meetjack_throughtunnel_step45", true},
        {L"AddativeIdle45", up, L"act6_3_meetjack_throughtunnel_step46", true},
    };
    CHECK(prompt::PlaceInRun(stages, L"act6_3_meetjack_throughtunnel_step45").step == 1);
    CHECK(prompt::PlaceInRun(stages, L"act6_3_meetjack_throughtunnel_step45").steps == 2);
    CHECK(prompt::PlaceInRun(stages, L"AddativeIdle43").step == 0);
    CHECK(prompt::PlaceInRun(stages, L"AddativeIdle45").step == 1);
    // A single stage that goes on to a stage of another input is no run.
    const std::vector<prompt::RunStage> lone{{L"swing1_QTE", L"IA_InteractionMovement_Right", L"swing1_to_swing2", false}};
    CHECK(prompt::PlaceInRun(lone, L"swing1_QTE").head.empty());
}
