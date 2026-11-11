// features/ChoiceRules: which options make a choice, which choice a pooled option widget belongs
// to, when a hold toward an option begins, when a choice is read, how it ended, and when the
// option taken is said.
#include "Support.hpp"
#include "features/ChoiceRules.hpp"

#include <set>
#include <string>
#include <vector>

using namespace da;
using choice::End;

namespace
{
    // The game's two pooled option widgets, as handles.
    int g_widgets[2];
    choice::Handle kLeftWidget = &g_widgets[0];
    choice::Handle kRightWidget = &g_widgets[1];

    choice::Option Opt(choice::Handle widget, long long direction, const wchar_t* text, bool timed = false)
    {
        choice::Option o;
        o.widget = widget;
        o.direction = direction;
        o.text = text;
        o.timed = timed;
        return o;
    }

    // The hold's reports as the game sends them: each value twice (its Blueprint, then the
    // native under it). Returns at which reports a hold began.
    std::vector<double> Holds(choice::Option& o, const std::vector<double>& values)
    {
        std::vector<double> began;
        for (const double v : values)
        {
            const bool first = choice::Hold(o, v);
            const bool second = choice::Hold(o, v);
            CHECK_FALSE(second);
            if (first) began.push_back(v);
        }
        return began;
    }
}

TEST_CASE("choice: how the game says a choice ended")
{
    CHECK(choice::EndOf(0) == End::None);
    CHECK(choice::EndOf(1) == End::Left);
    CHECK(choice::EndOf(2) == End::Right);
    CHECK(choice::EndOf(3) == End::TimedOut);
    CHECK(choice::EndOf(4) == End::Other);
}

TEST_CASE("choice: a hold's words are said as it begins, once per hold")
{
    SUBCASE("the ring filling from empty, with its small ups and downs")
    {
        choice::Option o = Opt(kLeftWidget, 0, L"Help Sam");
        CHECK(Holds(o, {0.0, 0.02, 0.1, 0.3, 0.5, 0.45, 0.5, 0.8, 1.0}) == std::vector<double>{0.1});
    }
    SUBCASE("the trigger let go and pressed again before the ring emptied")
    {
        choice::Option o = Opt(kLeftWidget, 0, L"Help Sam");
        CHECK(Holds(o, {0.2, 0.6, 0.45, 0.4, 0.42, 0.7}) == std::vector<double>{0.2, 0.42});
    }
    SUBCASE("the ring run empty and filled again")
    {
        choice::Option o = Opt(kLeftWidget, 0, L"Help Sam");
        CHECK(Holds(o, {0.3, 0.1, 0.0, 0.1, 0.5}) == std::vector<double>{0.3, 0.1});
    }
}

TEST_CASE("choice: the two sides set up together are one choice, read once both are in")
{
    choice::Book book;
    book.SetUp(Opt(kLeftWidget, 0, L"Help Sam"), 0.0);
    REQUIRE(book.All().size() == 1);
    CHECK_FALSE(choice::Book::DueToRead(book.All().back(), 0.1));
    book.SetUp(Opt(kRightWidget, 1, L"Go after Mike"), 0.2);
    REQUIRE(book.All().size() == 1);
    CHECK(choice::Book::DueToRead(book.All().back(), 0.2));
}

TEST_CASE("choice: a lone option is read after two seconds; a second of the same side is another choice")
{
    choice::Book book;
    book.SetUp(Opt(kLeftWidget, 0, L"Hide"), 0.0);
    CHECK_FALSE(choice::Book::DueToRead(book.All().back(), 1.9));
    CHECK(choice::Book::DueToRead(book.All().back(), 2.0));
    book.SetUp(Opt(kRightWidget, 0, L"Run"), 0.5);
    CHECK(book.All().size() == 2);
    // Nor does an option join a choice set up too long ago.
    choice::Book late;
    late.SetUp(Opt(kLeftWidget, 0, L"Hide"), 0.0);
    late.SetUp(Opt(kRightWidget, 1, L"Run"), 1.6);
    CHECK(late.All().size() == 2);
}

TEST_CASE("choice: the pooled option widgets belong to the latest choice that set them up")
{
    // The prologue, 2026-10-24: one choice after another on the same two widgets; the first,
    // finished, still listed them and took the next one's presses and verdict.
    choice::Book book;
    book.SetUp(Opt(kLeftWidget, 0, L"Take the bottle"), 0.0);
    book.SetUp(Opt(kRightWidget, 1, L"Leave it"), 0.1);
    choice::Choice* first = book.Of(kLeftWidget);
    REQUIRE(first != nullptr);
    first->announced = true;
    CHECK(book.Finish(*first, 1) == End::Left);
    book.SetUp(Opt(kLeftWidget, 0, L"Read the note"), 5.0);
    book.SetUp(Opt(kRightWidget, 1, L"Put it back"), 5.1);
    REQUIRE(book.All().size() == 2);
    CHECK(book.All().front().options.empty());
    choice::Option* option = nullptr;
    choice::Choice* second = book.Of(kRightWidget, &option);
    REQUIRE(second == &book.All().back());
    REQUIRE(option != nullptr);
    CHECK(option->text == L"Put it back");
    CHECK_FALSE(second->finished);
    std::wstring chosen;
    CHECK(book.Finish(*second, 2, &chosen) == End::Right);
    CHECK(chosen == L"Put it back");
}

TEST_CASE("choice: a verdict settles a choice once; none yet, or an end of the stage's own")
{
    choice::Book book;
    book.SetUp(Opt(kLeftWidget, 0, L"Hide"), 0.0);
    book.SetUp(Opt(kRightWidget, 1, L"Run", true), 0.1);
    choice::Choice& c = book.All().back();
    CHECK(book.Finish(c, 0) == End::None);
    CHECK_FALSE(c.finished);
    std::wstring chosen = L"untouched";
    CHECK(book.Finish(c, 3, &chosen) == End::TimedOut);
    CHECK(chosen == L"untouched");
    CHECK(c.finished);
    // The hooked event comes twice (the Blueprint's override and the native it calls).
    CHECK(book.Finish(c, 3) == End::None);
    choice::Book other;
    other.SetUp(Opt(kLeftWidget, 0, L"Hide"), 0.0);
    CHECK(other.Finish(other.All().back(), 4) == End::Other);
    CHECK(other.All().back().finished);
}

TEST_CASE("choice: a side taken whose option has no words leaves the words to say as they were")
{
    // The feature then says nothing for it, and keeps an earlier line still waiting.
    choice::Book book;
    book.SetUp(Opt(kLeftWidget, 0, L""), 0.0);
    book.SetUp(Opt(kRightWidget, 1, L"Run"), 0.1);
    std::wstring chosen = L"untouched";
    CHECK(book.Finish(book.All().back(), 1, &chosen) == End::Left);
    CHECK(chosen == L"untouched");
}

TEST_CASE("choice: options gone without a verdict end the choice; finished ones are forgotten, eight kept at most")
{
    choice::Book book;
    book.SetUp(Opt(kLeftWidget, 0, L"Hide"), 0.0);
    book.SetUp(Opt(kRightWidget, 1, L"Run"), 0.1);
    choice::Choice& c = book.All().back();
    c.announced = true;
    std::set<choice::Handle> alive{kLeftWidget};
    book.Prune([&](choice::Handle w) { return alive.contains(w); });
    CHECK(c.options.size() == 1);
    CHECK(choice::InProgress(c));
    alive.clear();
    book.Prune([&](choice::Handle w) { return alive.contains(w); });
    CHECK_FALSE(choice::Book::Gone(c, 1.5));
    CHECK(choice::Book::Gone(c, 2.5));
    CHECK_FALSE(choice::InProgress(c));
    c.finished = true;
    book.Forget(20.0);
    CHECK(book.All().size() == 1);
    book.Forget(31.0);
    CHECK(book.All().empty());
    choice::Book many;
    for (int i = 0; i < 12; ++i)
        many.SetUp(Opt(i % 2 ? kRightWidget : kLeftWidget, 0, L"Hide"), i * 2.0);
    CHECK(many.All().size() == choice::kChoicesKept);
}

TEST_CASE("choice: a side taken cuts what is being said, the option taken said at once in its place: Chris at the lodge, 2026-11-11")
{
    // research/sessions/s69-play.log, 10:43:37 to 10:43:38: R2 held toward УВЕРЕННОСТЬ, its words said
    // as the hold began; the game took the side at 38.161, its status card came at 38.598, and the
    // option taken waited for R2 to be let go (38.637), behind the card. Now the taking cuts in.
    choice::Chosen chosen;
    CHECK_FALSE(chosen.Waiting());
    CHECK_FALSE(chosen.Due(38.0, false, false).has_value());
    // The detailed level, the pad's trigger still held (no key repeats for the reader): said at once.
    CHECK(chosen.Take(L"УВЕРЕННОСТЬ, выбрано.", 38.161, false) == choice::Taking::Say);
    CHECK_FALSE(chosen.Waiting());
    CHECK_FALSE(chosen.Due(38.598, false, false).has_value());
    // The brief level: silence, and the card is the next thing said.
    CHECK(chosen.Take(L"", 50.0, false) == choice::Taking::Cut);
    CHECK_FALSE(chosen.Waiting());
}

TEST_CASE("choice: a side taken with a keyboard key still down waits for its release, at most two seconds")
{
    choice::Chosen chosen;
    // D held as the game takes the side: NVDA would stop at each of its repeats.
    CHECK(chosen.Take(L"Hide, chosen.", 1.0, true) == choice::Taking::Wait);
    CHECK(chosen.Waiting());
    CHECK_FALSE(chosen.Due(1.5, true, false).has_value());
    // Let go: the words, then what the game said meanwhile.
    const auto words = chosen.Due(1.6, false, false);
    REQUIRE(words.has_value());
    CHECK(words->compare(L"Hide, chosen.") == 0);
    CHECK_FALSE(chosen.Waiting());
    CHECK_FALSE(chosen.Due(1.7, false, false).has_value());
    // Held on: said after two seconds all the same.
    CHECK(chosen.Take(L"Leave it, chosen.", 5.0, true) == choice::Taking::Wait);
    CHECK_FALSE(chosen.Due(6.9, true, false).has_value());
    const auto late = chosen.Due(7.1, true, false);
    REQUIRE(late.has_value());
    CHECK(late->compare(L"Leave it, chosen.") == 0);
    // The brief level waits as well, with no words: the game's lines come once the key is up.
    CHECK(chosen.Take(L"", 10.0, true) == choice::Taking::Wait);
    const auto none = chosen.Due(10.3, false, false);
    REQUIRE(none.has_value());
    CHECK(none->empty());
}

TEST_CASE("choice: the option taken is not said over a quick-time event's key that came up while its key was down")
{
    // The prologue starts a quick-time event at once after a choice; its key cuts in over everything.
    choice::Chosen chosen;
    CHECK(chosen.Take(L"Hide, chosen.", 1.0, true) == choice::Taking::Wait);
    const auto words = chosen.Due(1.4, false, true);
    REQUIRE(words.has_value());
    CHECK(words->empty());
    CHECK_FALSE(chosen.Waiting());
}

TEST_CASE("choices: the players' share of an option is said as a whole percent, none while the game shows none")
{
    // UBatesDirectionalChoiceStage::OnShowGlobalStatsChanged hands each option its share as a quotient.
    CHECK(choice::SharePercent(0.43).compare(L"43%") == 0);
    CHECK(choice::SharePercent(0.0).compare(L"0%") == 0);
    CHECK(choice::SharePercent(1.0).compare(L"100%") == 0);
    CHECK(choice::SharePercent(0.576).compare(L"58%") == 0);
    CHECK(choice::SharePercent(-1.0).empty());
}

TEST_CASE("choices: a press toward an option says its words with the players' share where the game shows it")
{
    // Jessica's first run in the mine (research/sessions/s66-play.log, 16:36:39 to 16:36:42): the game
    // showed 76% beside СПРЯТАТЬСЯ, and the press toward it said the word alone.
    choice::Option hide;
    hide.text = L"СПРЯТАТЬСЯ";
    hide.globalShare = 0.759;
    CHECK(choice::HeldWords(hide).compare(L"СПРЯТАТЬСЯ, 76%") == 0);
    // No share shown (the setting off, or the game not finished once): the words alone.
    choice::Option run;
    run.text = L"БЕЖАТЬ";
    CHECK(choice::HeldWords(run).compare(L"БЕЖАТЬ") == 0);
    // The second line goes with the first, the share after both.
    choice::Option two;
    two.text = L"Left";
    two.subText = L"Hide";
    two.globalShare = 0.0;
    CHECK(choice::HeldWords(two).compare(L"Left. Hide, 0%") == 0);
    // An option with no words says no share alone.
    choice::Option empty;
    empty.globalShare = 0.5;
    CHECK(choice::HeldWords(empty).empty());
}
