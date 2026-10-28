// features/MenuRules: when an arriving screen is read, how a moving value is paced, when a change
// of the prompts is said, and the shape that tells a ticking count from new words.
#include "Support.hpp"
#include "features/MenuRules.hpp"

#include <string>
#include <vector>

using namespace da;

namespace
{
    // The menus run at about sixty frames a second (16.7 ms on the title screen); a frame is taken
    // as 1/64 s so that the sums stay exact. An arrival is weighed every frame, a value and the
    // prompts every eighth.
    constexpr double kFrame = 1.0 / 64.0;
    constexpr int kEighth = 8;
}

TEST_CASE("menu: a screen is read once its focus has landed and it is drawn, after a moment")
{
    menu::Arrival arrival;
    CHECK(arrival.Begin(0.0, false));
    CHECK_FALSE(arrival.Begin(0.1, false));
    bool anew = true;
    int asked = 0;
    const auto ready = [&]
    {
        ++asked;
        return true;
    };
    // Ready from the start, it still waits the settling time; readiness is not asked before.
    for (int frame = 0; frame * kFrame < menu::kSettle; ++frame)
        CHECK_FALSE(arrival.Due(frame * kFrame, ready, anew));
    CHECK(asked == 0);
    CHECK(arrival.Due(0.3125, ready, anew));
    CHECK_FALSE(anew);
    CHECK_FALSE(arrival.Pending());
}

TEST_CASE("menu: a screen whose focus never lands is read at the longest wait, and a page shown anew says so")
{
    menu::Arrival arrival;
    arrival.Begin(0.0, true);
    bool anew = false;
    const auto never = [] { return false; };
    CHECK_FALSE(arrival.Due(1.0, never, anew));
    CHECK_FALSE(arrival.Due(1.1875, never, anew));
    CHECK(arrival.Due(menu::kSettleMax, never, anew));
    CHECK(anew);
    // The page on display read: a new wait may begin.
    arrival.Clear();
    CHECK(arrival.Begin(2.0, false));
}

TEST_CASE("menu: a value moved under a held key is said at most every 0.35 s, and once more at rest")
{
    // The brightness slider, 2026-10-21: a held key moves it about every 0.15 s (here every tenth
    // frame).
    menu::Value value;
    value.Known(L"50");
    std::vector<std::wstring> said;
    std::vector<double> at;
    int level = 50;
    for (int frame = 0; frame < 128; ++frame)
    {
        const double now = frame * kFrame;
        if (frame % 10 == 0 && frame < 80) ++level;
        if (frame % kEighth != 0) continue;
        if (const auto words = value.Update(now, std::to_wstring(level)); !words.empty())
        {
            said.push_back(words);
            at.push_back(now);
        }
    }
    // Moving every 0.156 s, it is said every 0.375 s, the last time on the value it rests on.
    CHECK(said == std::vector<std::wstring>{L"53", L"55", L"58"});
    CHECK(at == std::vector<double>{0.375, 0.75, 1.125});
    // Nothing more once it has been said at rest.
    CHECK(value.Update(3.0, std::to_wstring(level)).empty());
    // Where the frames come quicker (every eighth of them 1/16 s apart), a value that stops soon
    // after a saying is said 0.15 s after it stopped, before the pace allows.
    menu::Value rest;
    rest.Known(L"1");
    CHECK(rest.Update(5.0, L"2") == L"2");
    CHECK(rest.Update(5.0625, L"3").empty());
    CHECK(rest.Update(5.125, L"3").empty());
    CHECK(rest.Update(5.1875, L"3").empty());
    CHECK(rest.Update(5.25, L"3") == L"3");
}

TEST_CASE("menu: a value said with its control is not said again, nor an empty one")
{
    menu::Value value;
    value.Known(L"On");
    CHECK(value.Update(1.0, L"On").empty());
    CHECK(value.Update(1.1, L"").empty());
    CHECK(value.Update(2.0, L"").empty());
    CHECK(value.Update(3.0, L"Off") == L"Off");
}

TEST_CASE("menu: a change of the prompts is said once it has stood half a second")
{
    menu::Steady steady;
    CHECK_FALSE(steady.Stood(0.0, true));
    CHECK_FALSE(steady.Stood(0.4, true));
    CHECK(steady.Stood(0.5, true));
    // A change that goes away before then is nothing.
    CHECK_FALSE(steady.Stood(1.0, true));
    CHECK_FALSE(steady.Stood(1.2, false));
    CHECK_FALSE(steady.Stood(1.6, true));
    steady.Reset();
    CHECK_FALSE(steady.Stood(2.0, true));
    CHECK(steady.Stood(2.5, true));
}

TEST_CASE("menu: a count that ticks keeps its shape, new words do not")
{
    CHECK(menu::Shape(L"Compiling shaders: 41%") == menu::Shape(L"Compiling shaders: 42%"));
    CHECK(menu::Shape(L"Compiling shaders: 41%") != menu::Shape(L"Loading: 41%"));
    CHECK(menu::Shape(L"12 of 36") == L" of ");
}
