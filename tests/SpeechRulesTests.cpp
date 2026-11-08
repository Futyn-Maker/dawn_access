// speech/SpeechRules: what a turned page cuts, and keeps of what was queued behind the page before.
#include "Support.hpp"
#include "speech/SpeechRules.hpp"

#include <string>
#include <vector>

using namespace da;
using speech::rules::Said;

TEST_CASE("a page turned while it is being read cuts it short, and says again what was queued behind it")
{
    // A page of 60 characters, three seconds' reading, and the transcript's key and a clue's card
    // queued behind it; the page turned a second in.
    const std::wstring page(60, L'п');
    const std::vector<Said> said{
        {L"announce", L"Accidentreport: крестик", 100.0},
        {L"announce", page, 101.0},
        {L"announce", L"Скрыть расшифровку: квадрат", 101.0},
        {L"announce", L"НАЙДЕНА УЛИКА. Письмо с угрозами. R1", 101.5},
    };
    const auto plan = speech::rules::Turn(said, page, 102.0);
    CHECK(plan.cut);
    REQUIRE(plan.kept.size() == 2);
    CHECK(tests::U8(plan.kept[0]) == "Скрыть расшифровку: квадрат");
    CHECK(tests::U8(plan.kept[1]) == "НАЙДЕНА УЛИКА. Письмо с угрозами. R1");
    // What came before the page was heard before it.
    const std::vector<Said> before{{L"announce", L"НАЙДЕНА УЛИКА. R1", 100.0}, {L"replace", page, 101.0}};
    const auto alone = speech::rules::Turn(before, page, 101.5);
    CHECK(alone.cut);
    CHECK(alone.kept.empty());
    // A line that cut in after the page (the player's own: a key, a repeat), or the reader silenced:
    // the page was gone already, and with it what was queued; the new page cuts in.
    const std::vector<Said> cut{{L"announce", page, 101.0}, {L"announce", L"card", 101.0}, {L"now", L"Путь дальше, 5 м.", 101.2}};
    CHECK(speech::rules::Turn(cut, page, 101.3).cut);
    CHECK(speech::rules::Turn(cut, page, 101.3).kept.empty());
    const std::vector<Said> stopped{{L"announce", page, 101.0}, {L"announce", L"card", 101.0}, {L"stop", L"", 101.2}};
    CHECK(speech::rules::Turn(stopped, page, 101.3).cut);
    CHECK(speech::rules::Turn(stopped, page, 101.3).kept.empty());
    // The page not among the lines: it cuts in, nothing to keep.
    CHECK(speech::rules::Turn(said, L"another page", 102.0).cut);
    CHECK(speech::rules::Turn(said, L"another page", 102.0).kept.empty());
    // The latest time the page was said counts.
    const std::vector<Said> twice{{L"announce", page, 100.0}, {L"now", L"x", 100.5}, {L"replace", page, 101.0}, {L"announce", L"card", 101.0}};
    const auto again = speech::rules::Turn(twice, page, 101.5);
    CHECK(again.cut);
    REQUIRE(again.kept.size() == 1);
    CHECK(tests::U8(again.kept[0]) == "card");
}

TEST_CASE("a page turned once the reader is past it cuts nothing, and the card being said is said once: the lodge's business card, 2026-11-07")
{
    // research/sessions/s56-full.log, 23:52:46.956 to 23:52:59.972: the card's front read (95
    // characters, 4.8 s), the clue's card said 12.5 s later as the turn revealed the back, and the back
    // shown 0.5 s after that. It cut the clue's card in and said it again whole before the back: Andrey
    // heard it twice, the second cutting the first.
    const std::wstring front = L"Мясоперерабатывающее предприятие \"Ла вьянд\" Джек Махоуни Оборудование для скотобоен 555-0127";
    const std::vector<Said> said{
        {L"announce", L"Скрыть расшифровку: квадрат, Назад: круг", 46.399},
        {L"announce", front, 46.956},
        {L"announce", L"НАЙДЕНА УЛИКА. Визитка. R1", 59.467},
    };
    const auto plan = speech::rules::Turn(said, front, 59.972);
    CHECK_FALSE(plan.cut);
    CHECK(plan.kept.empty());
    // Turned while the front was still being read, the front is cut and the card kept.
    const auto early = speech::rules::Turn(said, front, 50.0);
    CHECK(early.cut);
    CHECK(early.kept.size() == 1);
}

TEST_CASE("a page turned long after the lines behind it were heard says none of them again: the asylum's memo, 2026-11-07")
{
    // act4_2's administrator notes (research/sessions/s52-full.log, 18:35:09 to 18:35:46): the memo's
    // page of 640 characters, the transcript's key and the clue's card queued behind it, and the page
    // turned 37 s on. Andrey had heard the card, and it was said again before the next page.
    const std::wstring memo(640, L'м');
    const std::vector<Said> said{
        {L"announce", L"Назад: круг", 1.0},
        {L"announce", memo, 1.6},
        {L"announce", L"Скрыть расшифровку: квадрат", 1.6},
        {L"announce", L"НАЙДЕНА УЛИКА. Административные записи. R1", 2.3},
    };
    CHECK_FALSE(speech::rules::Turn(said, memo, 39.0).cut);
    CHECK(speech::rules::Turn(said, memo, 39.0).kept.empty());
    // Turned while the memo was still being read, both are kept.
    CHECK(speech::rules::Turn(said, memo, 20.0).cut);
    CHECK(speech::rules::Turn(said, memo, 20.0).kept.size() == 2);
    // Turned as the card was being said, the card is left to finish.
    const double cardDone = 1.6 + speech::rules::Speaking(memo) + speech::rules::Speaking(L"Скрыть расшифровку: квадрат") +
                            speech::rules::Speaking(L"НАЙДЕНА УЛИКА. Административные записи. R1");
    const auto late = speech::rules::Turn(said, memo, cardDone - 0.5);
    CHECK_FALSE(late.cut);
    CHECK(late.kept.empty());
}

TEST_CASE("a page turned as the clue's card is said leaves the card to finish: the asylum's telegram and the native tribes' letter")
{
    // The telegram (research/sessions/s52-full.log, 18:52:05 to 18:52:15): its first page (100
    // characters) and the transcript's key heard by 18:52:12; the clue's card came at 18:52:14.7, and the
    // page turned at 18:52:15.3. The key was said again before the card.
    const std::wstring page = L"ОТВЕТИТЬ НЕЗАМЕДЛИТЕЛЬНО: Репортеров и всех прочих, кто будет лезть с вопросами, не пускать ни в коем случае!";
    const std::vector<Said> said{
        {L"announce", L"Назад: круг", 4.6},
        {L"announce", page, 5.3},
        {L"announce", L"Скрыть расшифровку: квадрат", 5.3},
        {L"announce", L"НАЙДЕНА УЛИКА. Телеграмма насчет шахтеров. R1", 14.7},
    };
    const auto plan = speech::rules::Turn(said, page, 15.3);
    CHECK_FALSE(plan.cut);
    CHECK(plan.kept.empty());
    // act3_11's letter about the native tribes (research/sessions/s47-full.log, 22:43:24.958 to
    // 22:44:02.902): 606 characters, the key behind them, the clue's card 37.2 s on and the back page
    // 0.7 s after it, which cut the card short.
    const std::wstring letter(606, L'п');
    const std::vector<Said> tribes{
        {L"announce", letter, 0.0},
        {L"announce", L"Скрыть расшифровку: квадрат", 0.001},
        {L"announce", L"НАЙДЕНА УЛИКА. Письмо о коренных племенах Америки. R1", 37.208},
    };
    CHECK_FALSE(speech::rules::Turn(tribes, letter, 37.944).cut);
}

TEST_CASE("a page turned says the new page first, then what was kept behind the old one: Hannah's diary, 2026-11-08")
{
    // research/sessions/s58-whole.log, 11:23:07.025 to 11:23:09.570: the diary's second page
    // (57 characters, 2.9 s) said, the stick's prompt and the way back queued behind it, and the page
    // turned 2.5 s in. The kept prompts were said first and the third page after them; Andrey asked
    // for the page first.
    const std::wstring second = L"Обожемой!!! Майк тоже согласен!!! Он сегодня звонил Джошу";
    const std::wstring third = L"Ура! Завтра ТУСАНЕМ! Это будет ТАК КРУТО!";
    const std::vector<Said> said{
        {L"replace", second, 7.025},
        {L"announce", L"левый стик влево и вправо", 7.025},
        {L"announce", L"Назад: круг", 7.026},
    };
    const auto plan = speech::rules::Turn(said, second, 9.570);
    REQUIRE(plan.cut);
    REQUIRE(plan.kept.size() == 2);
    const auto order = speech::rules::TurnOrder(plan, third);
    REQUIRE(order.size() == 3);
    CHECK(tests::U8(order[0].text) == tests::U8(third));
    CHECK(order[0].interrupt);
    CHECK(tests::U8(order[1].text) == "левый стик влево и вправо");
    CHECK_FALSE(order[1].interrupt);
    CHECK(tests::U8(order[2].text) == "Назад: круг");
    CHECK_FALSE(order[2].interrupt);
    // The next page turned while the third is read keeps the prompts again: queued behind the page,
    // they cut nothing in.
    std::vector<Said> after;
    for (const auto& line : order)
        after.push_back({line.policy, line.text, 9.570});
    const auto next = speech::rules::Turn(after, third, 10.0);
    CHECK(next.cut);
    CHECK(next.kept.size() == 2);
    // The reader past the page: the new one waits behind what is said, alone.
    speech::rules::TurnPlan past;
    past.cut = false;
    const auto behind = speech::rules::TurnOrder(past, third);
    REQUIRE(behind.size() == 1);
    CHECK_FALSE(behind[0].interrupt);
}

TEST_CASE("a page shown again unchanged as it is being read is left to be read on: Jack's journal, 2026-11-08")
{
    // research/sessions/s61-play.log, 18:59:39.298 to 18:59:50.511: the journal's second page (700
    // characters, 35 s) said, the stick's prompt, the bar, the way on and the clue's card queued
    // behind it; Ashley's comment hid the transcript at 18:59:47 and the game showed it again 11.2 s
    // into the page. Cut in and said again, it was heard to its middle and then from its start.
    const std::wstring page(700, L'в');
    const std::vector<Said> said{
        {L"announce", page, 39.298},
        {L"announce", L"левый стик влево и вправо", 39.401},
        {L"announce", L"Прокрутить: левый стик вверх и вниз, Скрыть расшифровку: квадрат", 39.402},
        {L"announce", L"Дальше: левый стик влево.", 39.844},
        {L"announce", L"НАЙДЕНА УЛИКА. Дневник: каннибализм. R1", 39.899},
    };
    CHECK(speech::rules::StillOn(said, page, 50.511));
    // Read through, it is said again as a transcript shown again is.
    CHECK_FALSE(speech::rules::StillOn(said, page, 39.298 + 35.1));
    // Cut short meanwhile (the player's key, F7), it is said again too.
    auto stopped = said;
    stopped.push_back({L"stop", L"", 45.0});
    CHECK_FALSE(speech::rules::StillOn(stopped, page, 50.511));
    // Never said: nothing to read on.
    CHECK_FALSE(speech::rules::StillOn(said, std::wstring(10, L'x'), 50.511));
}
