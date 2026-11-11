// features/MenuRules: when an arriving screen is read, how a moving value is paced, when a change
// of the prompts is said, and the shape that tells a ticking count from new words.
#include "Support.hpp"
#include "features/MenuRules.hpp"

#include <string>
#include <vector>

using namespace da;
using tests::U8;

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

TEST_CASE("menu: the game's narration of a control is every line of its utterance, 2026-11-01")
{
    // The totems tab, as the game queued it (s37): a slot hovered queues its kind with the clearing
    // flag, then its place and, locked, the locked word, all in one frame.
    menu::Utterance slot;
    slot.Queued(L"Тотем смерти", true, 16124);
    slot.Queued(L"6", false, 16124);
    slot.Queued(L"Недоступно", false, 16124);
    CHECK(U8(slot.Line()) == "Тотем смерти, 6, Недоступно");
    // The next slot begins a new utterance, and a totem found has no locked word.
    slot.Queued(L"Тотем наставления", true, 16300);
    slot.Queued(L"1", false, 16300);
    CHECK(U8(slot.Line()) == "Тотем наставления, 1");
    // A butterfly's one line, queued without the clearing flag at a frame of its own, is its own
    // utterance.
    menu::Utterance butterfly;
    butterfly.Queued(L"КРЫСЫ, ТОЛЬКО ПУШИСТЫЕ", false, 15000);
    butterfly.Queued(L"Недоступно", false, 15100);
    CHECK(U8(butterfly.Line()) == "Недоступно");
    // An empty line adds nothing.
    butterfly.Queued(L"", false, 15100);
    CHECK(U8(butterfly.Line()) == "Недоступно");
}

TEST_CASE("menu: the badge of an item not looked at yet is a mark, not its name, 2026-11-01")
{
    // A totem found draws its badge and nothing else: it is unnamed, and the game's narration names
    // it, with the badge after.
    const std::vector<menu::Shown> totem{{L"НОВОЕ", true}};
    CHECK_FALSE(menu::Names(totem));
    CHECK(U8(menu::Named(totem, L"Тотем наставления, 1")) == "Тотем наставления, 1, НОВОЕ");
    // A butterfly and a clue draw their names; the badge follows the name wherever it sits.
    const std::vector<menu::Shown> butterfly{{L"КРЫСЫ, ТОЛЬКО ПУШИСТЫЕ", false}, {L"НОВОЕ", true}};
    CHECK(menu::Names(butterfly));
    CHECK(U8(menu::Named(butterfly, L"")) == "КРЫСЫ, ТОЛЬКО ПУШИСТЫЕ, НОВОЕ");
    const std::vector<menu::Shown> clue{{L"НОВОЕ", true}, {L"1.", false}, {L"Плакат", false}};
    CHECK(U8(menu::Named(clue, L"")) == "1., Плакат, НОВОЕ");
    // Nothing drawn and nothing narrated is no label.
    CHECK(menu::Named({}, L"").empty());
    CHECK(U8(menu::Named({}, L"Недоступно")) == "Недоступно");
}

TEST_CASE("menu: a panel of pictures heads them with its caption, a found one among them or not, 2026-11-01")
{
    // A totem column (WBP_TotemContainer): its grid of slots and, beside it, the caption's panel.
    // With a totem found the slot's badge named the grid, and the captions of the two columns with
    // a totem were read as the page's text: "Тотемы. Смерть. Наставление. Смерть. Недоступно".
    const menu::Controls none;
    const std::vector<menu::Shown> found{{L"НОВОЕ", true}};
    const menu::Controls grid{true, menu::Names(found)};
    CHECK(menu::LaysOutControls(none, {grid, none}));
    // A modal's question beside its row of named buttons is the page's own.
    CHECK_FALSE(menu::LaysOutControls(none, {{true, true}, none}));
    // A panel with controls of its own lays them out, named or not.
    CHECK(menu::LaysOutControls({true, true}, {}));
    // A panel with no controls anywhere lays out none.
    CHECK_FALSE(menu::LaysOutControls(none, {none, none}));
}

TEST_CASE("menu: a control the game's narration leaves nameless is named by its group's heading: the hunger totems, 2026-11-08")
{
    // research/sessions/s62-play.log, 21:39:07 to 21:39:09: the fortune column's slot queued its
    // kind and its place, the hunger column's slot an empty line in the kind's place.
    menu::Utterance slot;
    slot.Queued(L"Тотем удачи", true, 21912);
    slot.Queued(L"1", false, 21912);
    CHECK(U8(slot.Line()) == "Тотем удачи, 1");
    CHECK_FALSE(slot.Nameless());
    slot.Queued(L"", true, 22032);
    slot.Queued(L"1", false, 22032);
    CHECK(U8(slot.Line()) == "1");
    CHECK(slot.Nameless());
    // The next named slot is named again.
    slot.Queued(L"Тотем смерти", true, 22300);
    slot.Queued(L"5", false, 22300);
    CHECK_FALSE(slot.Nameless());
    CHECK(U8(slot.Line()) == "Тотем смерти, 5");
    // An empty line that does not clear adds nothing and names nothing away.
    slot.Queued(L"", false, 22300);
    CHECK_FALSE(slot.Nameless());
    CHECK(U8(slot.Line()) == "Тотем смерти, 5");
    // The column's caption, drawn over it, names the slot, its badge after as ever.
    CHECK(U8(menu::NamedByHeading(L"Голод", L"1, НОВОЕ")) == "Голод, 1, НОВОЕ");
    CHECK(U8(menu::NamedByHeading(L"", L"1, НОВОЕ")) == "1, НОВОЕ");
    CHECK(U8(menu::NamedByHeading(L"Голод", L"")) == "Голод");
}

TEST_CASE("menus: a question shown with its buttons is said with the button the focus comes to: the chapter select, 2026-11-09")
{
    // research/sessions/s65-play.log, 14:30:53.489: "10. ИТОГ" chosen, the page opens "Начать?" over
    // "Да" and "Нет" and puts the focus on "Да"; only "Да" was said.
    const std::set<std::wstring> said{menu::Shape(L"Измените свою судьбу")};
    const std::vector<std::wstring> body{L"Измените свою судьбу", L"Начать?"};
    CHECK(menu::TextsWithFocus(true, body, said) == std::vector<std::wstring>{L"Начать?"});
    // Within the group ("Да" to "Нет") the control alone.
    CHECK(menu::TextsWithFocus(false, body, said).empty());
    // Nothing new on the page: the control alone.
    CHECK(menu::TextsWithFocus(true, {L"Измените свою судьбу"}, said).empty());
    // A count that ticks is one text, digits aside.
    const std::set<std::wstring> ticking{menu::Shape(L"Осталось: 10")};
    CHECK(menu::TextsWithFocus(true, {L"Осталось: 9"}, ticking).empty());
}

TEST_CASE("menus: the chapter select's question is found beside its buttons, the page's own walk leaving it out: 2026-11-10")
{
    // WBP_Episodes (research/extract/json/.../WBP_Episodes.json): the root overlay holds the title, the
    // carousel, its two arrows (CarouselButtons: two BMUIButtons with no words), the input blocker and
    // the question (Confirmation: a vertical box of BMUIStyledTextBlock_45 "Начать?" over HorizontalBox_0
    // with "Да" and "Нет").
    const menu::Controls none{};
    const menu::Controls arrows{true, false};
    const menu::Controls answers{true, true};
    // The root lays out controls by its wordless arrows, so the page's walk stops there and finds no
    // body at all: the question had nothing to come with (research/sessions/s66-play.log, 16:05:28.737).
    CHECK(menu::LaysOutControls(none, {none, arrows, none}));
    // The question's own panel lays out no controls: its text asks over the row of named buttons.
    CHECK(menu::AsksOver(answers, none, {answers}));
    // A row of wordless controls is headed, not asked over; a panel that holds controls itself asks
    // nothing (a settings list with its headings); a panel with no controls in the row asks nothing.
    CHECK_FALSE(menu::AsksOver(arrows, none, {arrows}));
    CHECK_FALSE(menu::AsksOver(answers, answers, {}));
    CHECK_FALSE(menu::AsksOver(none, none, {none}));
    // The focus comes to "Да" from the carousel: the question goes before it, once.
    const std::set<std::wstring> said{};
    const auto body = menu::WithQuestion({}, {L"Начать?"});
    CHECK(menu::TextsWithFocus(true, body, said) == std::vector<std::wstring>{L"Начать?"});
    // A question the page's walk found already is not added twice.
    CHECK(menu::WithQuestion({L"Начать?", L"Внимание"}, {L"Начать?"}) == std::vector<std::wstring>{L"Начать?", L"Внимание"});
}

TEST_CASE("menus: the dialogue history is read a line at a time")
{
    // s71 20:09:37: the box's text, the newest line first.
    const auto lines = menu::HistoryLines({L"(Мэтт) иду.\n(Мэтт) Ага,\r\n\n(Эмили) Проснись, Мэтт! Ты пойдешь со мной за сумкой?\n  ", L"(Джош) Хорошо."});
    REQUIRE(lines.size() == 4);
    CHECK(lines[0] == L"(Мэтт) иду.");
    CHECK(lines[1] == L"(Мэтт) Ага,");
    CHECK(lines[2] == L"(Эмили) Проснись, Мэтт! Ты пойдешь со мной за сумкой?");
    CHECK(lines[3] == L"(Джош) Хорошо.");
    CHECK(menu::HistoryLines({L"", L"\n\n"}).empty());
    menu::LineCursor cursor;
    cursor.Set(lines.size());
    CHECK(cursor.At() == 0);
    CHECK(cursor.Move(-1) == 0); // held at the newest
    CHECK(cursor.Move(1) == 1);
    CHECK(cursor.Move(1) == 2);
    CHECK(cursor.Move(1) == 3);
    CHECK(cursor.Move(1) == 3); // held at the oldest
    CHECK(cursor.Move(-1) == 2);
    menu::LineCursor none;
    CHECK(none.Move(1) == 0);
}
