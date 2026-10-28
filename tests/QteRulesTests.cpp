// features/QteRules: how a quick-time event's prompt ends, when its key is said again, its cue's
// direction, and which of a settings page's examples are said.
#include "Support.hpp"
#include "features/QteRules.hpp"

#include <string>
#include <vector>

using namespace da;
using qte::Direction;
using qte::Outcome;

namespace
{
    // The QTE poller runs every frame; a frame every 1/32 s keeps the sums exact.
    constexpr double kFrame = 1.0 / 32.0;

    qte::Stage Driven()
    {
        qte::Stage s;
        s.known = true;
        s.live = true;
        return s;
    }

    // Polls a prompt set up at 0 from `from` to `to` with the same reading, and returns the
    // step that finished it (or the last), and the times its key was said again.
    struct Run
    {
        qte::Step last;
        double finishedAt = -1.0;
        std::vector<double> repeats;
    };

    Run Play(qte::Prompt& p, double from, double to, bool drawn, const qte::Stage& stage)
    {
        Run run;
        for (int frame = static_cast<int>(from / kFrame); frame * kFrame < to; ++frame)
        {
            const double now = frame * kFrame;
            if (p.finished) break;
            const auto step = qte::Poll(p, now, drawn, stage);
            run.last = step;
            if (step.finish)
            {
                p.finished = true;
                run.finishedAt = now;
                break;
            }
            if (step.repeat)
            {
                p.spokenAt = now;
                run.repeats.push_back(now);
            }
        }
        return run;
    }
}

TEST_CASE("qte: the game's verdicts, and an end with none")
{
    CHECK(qte::OutcomeOf(1) == Outcome::Success);
    CHECK(qte::OutcomeOf(2) == Outcome::Failure);
    CHECK(qte::OutcomeOf(3) == Outcome::Timeout);
    for (const long long none : {0LL, 4LL, -1LL, 255LL})
        CHECK(qte::OutcomeOf(none) == Outcome::None);
}

TEST_CASE("qte: the cue's direction from the game's action names, the last word deciding")
{
    CHECK(qte::DirectionOf(L"IA_QTEFaceButtonBottom") == Direction::Down);
    CHECK(qte::DirectionOf(L"IA_QTEFaceButtonTop") == Direction::Up);
    CHECK(qte::DirectionOf(L"IA_QTEFaceButtonLeft") == Direction::Left);
    CHECK(qte::DirectionOf(L"IA_QTEFaceButtonRight") == Direction::Right);
    CHECK(qte::DirectionOf(L"IA_QTELeftStickUp") == Direction::Up);
    CHECK(qte::DirectionOf(L"IA_QTELeftStickDown") == Direction::Down);
    CHECK(qte::DirectionOf(L"IA_QTELeftStickLeft") == Direction::Left);
    CHECK(qte::DirectionOf(L"IA_QTELeftStickRight") == Direction::Right);
    CHECK(qte::DirectionOf(L"") == Direction::Up);
}

TEST_CASE("qte: the stage's verdict ends the prompt, the key said again each second until then")
{
    qte::Prompt p;
    p.spokenAt = 0.0; // the key said as the prompt came up
    qte::Stage stage = Driven();
    Run run = Play(p, 0.0, 2.625, true, stage);
    CHECK(run.finishedAt < 0.0);
    CHECK(run.repeats == std::vector<double>{1.0, 2.0});
    stage.verdict = 1;
    run = Play(p, 2.625, 4.0, true, stage);
    CHECK(run.finishedAt == doctest::Approx(2.625));
    CHECK(run.last.result == 1);
    CHECK(std::wstring(run.last.from) == L"the stage");
}

TEST_CASE("qte: a stage that moved on ends the prompt with no verdict")
{
    SUBCASE("its widget handed back")
    {
        qte::Prompt p;
        qte::Stage stage = Driven();
        stage.drivesIt = false;
        const Run run = Play(p, 0.0, 1.0, true, stage);
        CHECK(run.finishedAt == doctest::Approx(0.0));
        CHECK(run.last.result == 0);
        CHECK(std::wstring(run.last.from) == L"the stage, which moved on");
    }
    SUBCASE("the stage over")
    {
        qte::Prompt p;
        qte::Stage stage = Driven();
        stage.started = false;
        CHECK(Play(p, 0.0, 1.0, true, stage).last.result == 0);
    }
}

TEST_CASE("qte: a verdict told in the poll the stage moves on, or the prompt goes away, is still the verdict")
{
    // The likeliest last poll of a prompt: the stage has its verdict and has let the prompt go in
    // the same moment. The verdict, with its tone, is what counts.
    SUBCASE("its widget handed back with the verdict")
    {
        qte::Prompt p;
        qte::Stage stage = Driven();
        stage.verdict = 2;
        stage.drivesIt = false;
        const Run run = Play(p, 0.0, 1.0, true, stage);
        CHECK(run.finishedAt == doctest::Approx(0.0));
        CHECK(run.last.result == 2);
        CHECK(std::wstring(run.last.from) == L"the stage");
    }
    SUBCASE("the stage over with the verdict")
    {
        qte::Prompt p;
        qte::Stage stage = Driven();
        stage.verdict = 3;
        stage.started = false;
        CHECK(Play(p, 0.0, 1.0, true, stage).last.result == 3);
    }
    SUBCASE("the verdict in the poll the prompt, long undrawn, would be over")
    {
        qte::Prompt p;
        p.spokenAt = 0.0;
        const double over = 1.0 + kFrame + 1.5 + kFrame; // as a stage that says nothing lets it go
        CHECK(Play(p, 0.0, over, false, Driven()).finishedAt < 0.0);
        qte::Stage stage = Driven();
        stage.verdict = 1;
        const Run run = Play(p, over, over + 1.0, false, stage);
        CHECK(run.finishedAt == doctest::Approx(over));
        CHECK(run.last.result == 1);
    }
}

TEST_CASE("qte: a prompt no longer drawn is over, after its first second, at once without a stage and after 1.5 s with one")
{
    SUBCASE("without a stage")
    {
        qte::Prompt p;
        p.spokenAt = 0.0;
        // The first poll past the first second marks it hidden, the next ends it.
        const Run run = Play(p, 0.0, 3.0, false, qte::Stage{});
        CHECK(run.finishedAt == doctest::Approx(1.0 + 2 * kFrame));
        CHECK(run.last.result == 0);
        CHECK(std::wstring(run.last.from) == L"the prompt, which went away");
    }
    SUBCASE("with a stage that says nothing yet")
    {
        qte::Prompt p;
        p.spokenAt = 0.0;
        const Run run = Play(p, 0.0, 4.0, false, Driven());
        CHECK(run.finishedAt == doctest::Approx(1.0 + kFrame + 1.5 + kFrame));
    }
    SUBCASE("with a stage gone: its verdict will not come, so its own flags decide, after 1.5 s")
    {
        qte::Prompt p;
        p.spokenAt = 0.0;
        qte::Stage gone;
        gone.known = true;
        const Run run = Play(p, 0.0, 4.0, false, gone);
        CHECK(run.finishedAt > 2.5);
    }
    SUBCASE("drawn again in time, it goes on")
    {
        qte::Prompt p;
        p.spokenAt = 0.0;
        Play(p, 0.0, 2.0, false, Driven());
        const Run run = Play(p, 2.0, 5.0, true, Driven());
        CHECK(run.finishedAt < 0.0);
    }
}

TEST_CASE("qte: no key again while the prompt is paused, nor for a settings page's example")
{
    qte::Prompt paused;
    paused.paused = true;
    CHECK(Play(paused, 0.0, 3.0, true, Driven()).repeats.empty());
    qte::Prompt example;
    example.demonstration = true;
    CHECK(Play(example, 0.0, 3.0, true, qte::Stage{}).repeats.empty());
}

TEST_CASE("qte: what the game says by itself waits only while a prompt runs")
{
    qte::Prompt p;
    CHECK(qte::Running(p));
    p.paused = true;
    CHECK_FALSE(qte::Running(p));
    p.paused = false;
    p.demonstration = true;
    CHECK_FALSE(qte::Running(p));
    p.demonstration = false;
    p.finished = true;
    CHECK_FALSE(qte::Running(p));
}

TEST_CASE("qte: the same set-up told twice is one; another action, or later, is another")
{
    CHECK(qte::SameSetUp(L"IA_QTELeftStickUp", 1.0, L"IA_QTELeftStickUp", 1.2));
    CHECK_FALSE(qte::SameSetUp(L"IA_QTELeftStickUp", 1.0, L"IA_QTELeftStickUp", 1.6));
    CHECK_FALSE(qte::SameSetUp(L"IA_QTELeftStickUp", 1.0, L"IA_QTEFaceButtonBottom", 1.2));
}

TEST_CASE("qte: an example shown in several places at once is said once")
{
    qte::Examples examples;
    CHECK(examples.Says(L"cross button", 0.0));
    CHECK_FALSE(examples.Says(L"cross button", 0.1));
    CHECK(examples.Says(L"circle button", 0.2));
    CHECK(examples.Says(L"cross button", 0.3));
    CHECK(examples.Says(L"cross button", 1.0));
}
