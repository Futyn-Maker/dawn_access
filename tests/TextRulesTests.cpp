// features/TextRules: which subtitles are new, and which lines of a HUD element are said.
#include "Support.hpp"
#include "features/TextRules.hpp"

#include <string>
#include <vector>

using namespace da;
using Lines = std::vector<std::wstring>;

TEST_CASE("lines: a subtitle is said as it appears, and not again while it stays or blinks for a frame")
{
    lines::Subtitles subtitles;
    CHECK(subtitles.Update({L"Beth: Hannah?"}) == Lines{L"Beth: Hannah?"});
    CHECK(subtitles.Update({L"Beth: Hannah?"}).empty());
    // Gone for a frame, back the next: the two polls before still hold it.
    CHECK(subtitles.Update({}).empty());
    CHECK(subtitles.Update({L"Beth: Hannah?"}).empty());
    // Two new lines at once (a subtitle wrapped, or two speakers): together, once each.
    CHECK(subtitles.Update({L"Sam: Over here!", L"Josh: Wait!", L"Sam: Over here!"}) == Lines{L"Sam: Over here!", L"Josh: Wait!"});
    CHECK(subtitles.Shown().size() == 3);
    subtitles.Reset();
    CHECK(subtitles.Shown().empty());
    CHECK(subtitles.Update({L"Josh: Wait!"}) == Lines{L"Josh: Wait!"});
}

TEST_CASE("lines: a HUD element is said once its lines stand two polls, new lines only")
{
    lines::Element element;
    CHECK(element.Shown({L"Find Hannah"}, 0.0).empty());
    CHECK(element.Shown({L"Find Hannah"}, 0.2).empty());
    CHECK(element.Shown({L"Find Hannah"}, 0.4) == Lines{L"Find Hannah"});
    CHECK(element.Shown({L"Find Hannah"}, 0.6).empty());
    // A title over a sequence of sentences: the title once, each sentence as it comes.
    CHECK(element.Shown({L"Find Hannah", L"She went outside"}, 0.8).empty());
    CHECK(element.Shown({L"Find Hannah", L"She went outside"}, 1.0).empty());
    CHECK(element.Shown({L"Find Hannah", L"She went outside"}, 1.2) == Lines{L"She went outside"});
    CHECK(element.Seen() == Lines{L"Find Hannah", L"She went outside"});
}

TEST_CASE("lines: the camcorder's clock is said at every tick it shows, and a blink is the same line")
{
    // The prologue's prank film, 2026-10-25: the timecode was cut to its first value.
    lines::Element clock;
    double now = 0.0;
    std::vector<Lines> said;
    for (const auto* tick : {L"0:21", L"0:21", L"0:21", L"0:22", L"0:22", L"0:22", L"0:23", L"0:23", L"0:23"})
    {
        const auto fresh = clock.Shown({tick}, now);
        if (!fresh.empty()) said.push_back(fresh);
        now += 0.2;
    }
    CHECK(said == std::vector<Lines>{{L"0:21"}, {L"0:22"}, {L"0:23"}});
    // A line that leaves and is back within two seconds blinked: not said again.
    lines::Element blink;
    blink.Shown({L"Press R1"}, 0.0);
    blink.Shown({L"Press R1"}, 0.2);
    CHECK(blink.Shown({L"Press R1"}, 0.4) == Lines{L"Press R1"});
    blink.Shown({}, 0.6);
    blink.Shown({}, 0.8);
    blink.Shown({}, 1.0);
    blink.Shown({L"Press R1"}, 1.2);
    blink.Shown({L"Press R1"}, 1.4);
    CHECK(blink.Shown({L"Press R1"}, 1.6).empty());
    // Back after longer: new again.
    lines::Element back;
    back.Shown({L"Press R1"}, 0.0);
    back.Shown({L"Press R1"}, 0.2);
    back.Shown({L"Press R1"}, 0.4);
    back.Shown({}, 0.6);
    back.Shown({}, 0.8);
    back.Shown({}, 3.0);
    back.Shown({L"Press R1"}, 3.2);
    back.Shown({L"Press R1"}, 3.4);
    CHECK(back.Shown({L"Press R1"}, 3.6) == Lines{L"Press R1"});
}

TEST_CASE("lines: hidden and shown again, an element is read again")
{
    lines::Element element;
    element.Shown({L"Totem found"}, 0.0);
    element.Shown({L"Totem found"}, 0.2);
    CHECK(element.Shown({L"Totem found"}, 0.4) == Lines{L"Totem found"});
    element.Hidden();
    element.Shown({L"Totem found"}, 0.6);
    element.Shown({L"Totem found"}, 0.8);
    CHECK(element.Shown({L"Totem found"}, 1.0) == Lines{L"Totem found"});
}

TEST_CASE("lines: a document's transcript hidden with its key and shown again is read again, 2026-11-01")
{
    // The wanted poster: its reading widget read at 17:26:51; the transcript hidden at 17:28:00
    // (the widget faded to no opacity and stayed on the screen's list, its text laid out as
    // before), shown at 17:28:50 and not read; hidden at 17:28:56, shown at 17:28:58, not read.
    const Lines poster{L"WANTED MILGREM, VICTOR Sex: Male Height: 6'2\""};
    lines::Element element;
    std::vector<double> said;
    const auto play = [&](double from, double to, bool drawn)
    {
        for (double t = from; t < to - 1e-9; t += 0.2)
        {
            if (!element.Shown(poster, t, drawn).empty()) said.push_back(t);
        }
    };
    play(0.0, 69.0, true);
    play(69.0, 119.0, false);
    play(119.0, 125.0, true);
    play(125.0, 127.4, false);
    play(127.4, 130.0, true);
    REQUIRE(said.size() == 3);
    CHECK(said[0] < 1.0);
    CHECK(said[1] >= 119.0);
    CHECK(said[1] < 120.0);
    CHECK(said[2] >= 127.4);
    CHECK(said[2] < 128.4);
    // Hidden, it shows nothing, so the screen is read without it.
    play(130.0, 131.0, false);
    CHECK(element.Seen().empty());
    // Faded out and back within a blink: not read again.
    play(131.0, 134.0, true);
    CHECK(said.size() == 3);
}

namespace
{
    // The HUD asks for the objective every thirtieth frame: twice a second at the game's usual pace.
    constexpr double kObjectivePoll = 0.5;

    struct Said
    {
        double at;
        std::wstring words;
        bool operator==(const Said&) const = default;
    };

    // Polls an objective timeline from `from` to `to`: `objective(t)` is what the game keeps,
    // `free(t)` whether play runs with no title card up.
    template <typename Objective, typename Free>
    std::vector<Said> Run(lines::Objective& rule, double from, double to, Objective objective, Free free)
    {
        std::vector<Said> said;
        for (double t = from; t < to; t += kObjectivePoll)
        {
            const auto step = rule.Update(t, objective(t), free(t));
            if (!step.say.empty()) said.push_back({t, step.say});
        }
        return said;
    }
}

TEST_CASE("lines: a chapter's first objective waits for the card that names the chapter, and comes after it")
{
    // Chapter one, 2026-10-25 (session 29): the state turned to play at 12:14:26.748; the chapter's
    // card was up from 27.361 to 37.391, its words fading in from 28.919; the scene's script set the
    // objective by 27.488, and the mod said it then, before "Chapter 1". Times from the turn to play.
    const std::wstring goal = L"Reach the cable car station.";
    const auto objective = [&](double t) { return t >= 0.75 ? goal : std::wstring(); };
    const auto cardUp = [](double t) { return t >= 0.5 && t < 10.625; };
    lines::Objective rule;
    const auto said = Run(rule, 0.25, 20.0, objective, [&](double t) { return !cardUp(t); });
    // Not before the card has gone and play has run free for a second.
    CHECK(said == std::vector<Said>{{11.75, goal}});
    CHECK(rule.Current() == goal);
    CHECK_FALSE(rule.Due());
}

TEST_CASE("lines: in free play an objective is said at the poll that finds it; unchanged, it is not said again")
{
    lines::Objective rule;
    const auto first = Run(rule, 0.0, 5.0, [](double t) { return t >= 3.0 ? std::wstring(L"Find Hannah") : std::wstring(); }, [](double) { return true; });
    CHECK(first == std::vector<Said>{{3.0, L"Find Hannah"}});
    CHECK(Run(rule, 5.0, 10.0, [](double) { return std::wstring(L"Find Hannah"); }, [](double) { return true; }).empty());
}

TEST_CASE("lines: an objective set under a card or a pause gives way to a newer one, and one turned off is not said")
{
    lines::Objective rule;
    // Two objectives in a row while the game is paused: only the second is said, a second after
    // the pause.
    const auto paused = [](double t) { return t >= 1.0 && t < 6.0; };
    const auto objective = [](double t) { return t < 2.0 ? std::wstring() : (t < 4.0 ? std::wstring(L"Find Hannah") : std::wstring(L"Follow Beth")); };
    const auto said = Run(rule, 0.0, 9.0, objective, [&](double t) { return !paused(t); });
    CHECK(said == std::vector<Said>{{7.0, L"Follow Beth"}});
    // Set and turned off again before play runs free: nothing.
    lines::Objective off;
    const auto gone =
        Run(off, 0.0, 9.0, [](double t) { return t >= 2.0 && t < 4.0 ? std::wstring(L"Find Hannah") : std::wstring(); }, [&](double t) { return !paused(t); });
    CHECK(gone.empty());
    CHECK_FALSE(off.Due());
}

namespace
{
    // A turn's opening polled from `from` to `to`: `loading(t)` a level loads (or another character
    // is given), `card(t)` the card that changes the character is up, `handed` when the player is
    // handed the character; free play runs where `free(t)` and no card is up.
    template <typename Objective, typename Free, typename Card>
    std::vector<Said> RunTurn(lines::Objective& rule, double from, double to, Objective objective, Free free, Card card, double loadedAt,
                              double handed)
    {
        std::vector<Said> said;
        for (double t = from; t < to; t += kObjectivePoll)
        {
            if (t <= loadedAt) rule.Opening();
            if (card(t)) rule.Card();
            if (t >= handed) rule.HandedOver();
            const auto step = rule.Update(t, objective(t), free(t) && !card(t));
            if (!step.say.empty()) said.push_back({t, step.say});
        }
        return said;
    }
}

TEST_CASE("lines: a turn's objective comes after the card that names the character, not before it, 2026-11-05")
{
    // Sam at the lodge (research/sessions/s45-full.log, from 23:04:00): the level loaded until 42.07,
    // the objective was set by 42.58, play ran from 45.36, the card "SAM, Washington Lodge, 20:44" came
    // at 65.86 (some 6 s up), and she was handed over at 159. The mod said the objective at 47.09, 19 s
    // before the card.
    const std::wstring sam = L"Find Josh and ask about the hot water.";
    lines::Objective rule;
    auto said = RunTurn(
        rule, 40.0, 170.0, [&](double t) { return t >= 42.58 ? sam : std::wstring(); }, [](double t) { return t >= 45.36; },
        [](double t) { return t >= 65.86 && t < 71.9; }, 42.07, 159.0);
    REQUIRE(said.size() == 1);
    CHECK(said[0].words == sam);
    CHECK(said[0].at > 71.9);
    CHECK(said[0].at < 74.0);
    // Matt (from 23:46:00): loaded until 46.84, the objective by 47.48, play from 48.30, his card at
    // 52.35; said at 49.80 before, 2.6 s before "MATT".
    const std::wstring matt = L"Go down the slope to get Emily's bag.";
    lines::Objective second;
    said = RunTurn(
        second, 40.0, 120.0, [&](double t) { return t >= 47.48 ? matt : std::wstring(); }, [](double t) { return t >= 48.30; },
        [](double t) { return t >= 52.35 && t < 58.4; }, 46.84, 1000.0);
    REQUIRE(said.size() == 1);
    CHECK(said[0].at > 58.4);
    CHECK(said[0].at < 60.5);
}

TEST_CASE("lines: a turn waits for the card that changes the character, not the chapter's card or the character's introduction")
{
    // Chapter two's start (research/sessions/s39.log, from 10:40:00): the chapter's card at 17 to 27
    // and Jessica's introduction at 29 to 39 (title cards, not the change); the scene set her objective
    // at 68.22, in free play, and her change card came at 68.96. The mod said it at 68.22.
    const std::wstring goal = L"Find another way into the house.";
    const auto titleCard = [](double t) { return (t >= 17.0 && t < 27.0) || (t >= 29.0 && t < 39.0); };
    lines::Objective rule;
    const auto said = RunTurn(
        rule, 0.0, 120.0, [&](double t) { return t >= 68.22 ? goal : std::wstring(); }, [&](double t) { return t >= 5.0 && !titleCard(t); },
        [](double t) { return t >= 68.96 && t < 75.0; }, 4.0, 1000.0);
    REQUIRE(said.size() == 1);
    CHECK(said[0].at > 75.0);
}

TEST_CASE("lines: with no card in a turn its objective comes at the hand-over, and one set mid-turn at once")
{
    lines::Objective rule;
    auto said = RunTurn(
        rule, 0.0, 40.0, [](double t) { return t >= 2.0 ? std::wstring(L"Follow Emily") : std::wstring(); }, [](double t) { return t >= 3.0; },
        [](double) { return false; }, 1.0, 30.0);
    REQUIRE(said.size() == 1);
    CHECK(said[0].at >= 30.0);
    CHECK(said[0].at < 31.5);
    CHECK_FALSE(rule.Opens());
    // The boiler's objective, set after a talk in the middle of Sam's turn (23:23:41): said at once.
    said = Run(rule, 40.0, 50.0, [](double t) { return t >= 41.0 ? std::wstring(L"Light the boiler with Josh") : std::wstring(L"Follow Emily"); },
               [](double) { return true; });
    REQUIRE(said.size() == 1);
    CHECK(said[0].at < 42.5);
}

TEST_CASE("lines: whether the objective changed is told at the poll that finds it, said or not")
{
    lines::Objective rule;
    auto step = rule.Update(0.0, L"", true);
    CHECK(step.changed);
    CHECK(rule.Known());
    step = rule.Update(0.5, L"Find Hannah", false);
    CHECK(step.changed);
    CHECK(step.say.empty());
    CHECK(rule.Due());
    step = rule.Update(1.0, L"Find Hannah", false);
    CHECK_FALSE(step.changed);
}

TEST_CASE("lines: the same words through two elements at once are said once")
{
    lines::Echo echo;
    CHECK(echo.Says(L"Find Hannah", 0.0));
    CHECK_FALSE(echo.Says(L"Find Hannah", 0.1));
    CHECK(echo.Says(L"Go outside", 0.2));
    CHECK(echo.Says(L"Find Hannah", 0.3));
    CHECK_FALSE(echo.Says(L"Find Hannah", 5.3));
    CHECK(echo.Says(L"Find Hannah", 5.4));
}

TEST_CASE("lines: an update card is read once, with its name, when its body is drawn, 2026-11-02")
{
    // WBP_ButterflyHUDUpdate at act2_1 (s39): the title and the key came up at 10:24:13.99, the
    // name 1.91 s later; the card was said twice, "Эффект бабочки: R1" and then whole.
    const Lines titleOnly{L"Эффект бабочки: R1"};
    const Lines whole{L"Эффект бабочки", L"Хранитель секретов", L"R1"};
    const auto play = [&](double bodyAt, double end)
    {
        lines::Element element;
        lines::Card card;
        std::vector<Lines> said;
        // The HUD is polled every sixth frame, a tenth of a second.
        for (double t = 0.0; t < end; t += 0.1)
        {
            const bool drawn = t >= bodyAt;
            if (!card.Ready(t, drawn)) continue;
            const auto fresh = element.Shown(drawn ? whole : titleOnly, t);
            if (!fresh.empty()) said.push_back(fresh);
        }
        return said;
    };
    const auto said = play(1.91, 5.0);
    REQUIRE(said.size() == 1);
    CHECK(said[0] == whole);
    // A card whose body never comes is read after the longest wait, as it stands.
    const auto bodiless = play(100.0, 5.0);
    REQUIRE(bodiless.size() == 1);
    CHECK(bodiless[0] == titleOnly);
    // A card hidden and shown again waits anew.
    lines::Card card;
    CHECK_FALSE(card.Ready(0.0, false));
    CHECK(card.Ready(lines::kCardWait, false));
    card.Hidden();
    CHECK_FALSE(card.Ready(10.0, false));
    CHECK(card.Ready(10.1, true));
}

TEST_CASE("a HUD hint shown with a control's prompt is still to be said until it settles, and the prompt waits for it")
{
    // The lodge's first totem, 2026-11-04 01:10:03 (research/sessions/s42.log): the inspection's
    // hint "Осмотрите тотем, чтобы заглянуть в будущее" came up 0.19 s before the stick's prompt and
    // settled 0.11 s after it, so the stick was said first; at the second totem the hint was said a
    // millisecond before the stick. Until the hint is said it is still to be said (hud::TextSettling
    // holds the prompts that long), and once said it is not.
    lines::Element hint;
    const std::vector<std::wstring> text{L"Осмотрите тотем, чтобы заглянуть в будущее"};
    CHECK(hint.Unsaid(text, 0.0));
    CHECK(hint.Shown(text, 0.0).empty());
    CHECK(hint.Unsaid(text, 0.0));
    CHECK(hint.Shown(text, 0.2).empty());
    CHECK(hint.Unsaid(text, 0.2));
    CHECK(hint.Shown(text, 0.4) == text);
    CHECK_FALSE(hint.Unsaid(text, 0.4));
    // Shown on, the words are not news; long gone and back, they are.
    CHECK_FALSE(hint.Unsaid(text, 0.4 + lines::kLineForgotten - 0.1));
    hint.Hidden();
    CHECK(hint.Unsaid(text, 5.0));
    // Nothing shown is nothing to wait for.
    CHECK_FALSE(hint.Unsaid({}, 5.0));
}

TEST_CASE("a document's page put in place of the one before cuts it short, 2026-11-06")
{
    // research/sessions/s46-full.log, the cabin's book of native tales (21:07:22 to 21:08:51): its cover
    // came first, then the she-bear's page once Mike opened it, the signs' page with the stick left, and
    // back and forth; each was queued behind the last, the whole of the one before said first.
    lines::Reading reading;
    CHECK_FALSE(reading.Interrupts()); // the cover: after what is being said
    CHECK(reading.Interrupts());       // the she-bear
    CHECK(reading.Interrupts());       // the signs
    CHECK(reading.Interrupts());       // the she-bear again
    // Handed back and on to the Kamasutra at 21:12:26: its cover waits its turn again.
    reading.Ended();
    CHECK_FALSE(reading.Interrupts());
    CHECK(reading.Interrupts());
}
