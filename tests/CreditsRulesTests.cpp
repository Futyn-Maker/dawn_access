// features/CreditsRules: the epilogue's credits kept and read on the player's key, and the
// interviews' overlay.
#include "Support.hpp"
#include "features/CreditsRules.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

using namespace da;
using Lines = std::vector<std::wstring>;

TEST_CASE("credits: a line of the roll is its entries side by side, the empty ones left out")
{
    // research/extract/json/Bates/Content/UI/Data/Credits/DA_Credits.json: a role and a name, a name
    // going on a list (its left column empty), two names side by side, a heading, a gap.
    CHECK(credits::LineOf({L"Creative Director", L"Neil McEwan"}) == L"Creative Director, Neil McEwan");
    CHECK(credits::LineOf({L"", L"Max Brode"}) == L"Max Brode");
    CHECK(credits::LineOf({L"Jez Harris", L"Steve Goss"}) == L"Jez Harris, Steve Goss");
    CHECK(credits::LineOf({L"Founders"}) == L"Founders");
    CHECK(credits::LineOf({L" ", L""}).empty());
    // An empty entry after a word, or between two, leaves no comma behind.
    CHECK(credits::LineOf({L"Jez Harris", L""}) == L"Jez Harris");
    CHECK(credits::LineOf({L"Graphics", L" ", L"Gavin Clarke"}) == L"Graphics, Gavin Clarke");
    CHECK(credits::LineOf({}).empty());
    // Line breaks and runs of space inside an entry are one space.
    CHECK(credits::LineOf({L"Unreal® Engine,\r\nCopyright  1998"}) == L"Unreal® Engine, Copyright 1998");
}

TEST_CASE("credits: the roll is read on the key, all of it the first time, then what came since, else nothing new")
{
    credits::Roll roll;
    CHECK(roll.Unread().empty());
    roll.Add(L"Ballistic Moon");
    roll.Add(L"Founders");
    roll.Add(L"Creative Director, Neil McEwan");
    CHECK(roll.AnyUnread());
    CHECK(roll.Unread() == Lines{L"Ballistic Moon", L"Founders", L"Creative Director, Neil McEwan"});
    CHECK_FALSE(roll.AnyUnread());
    CHECK(roll.Unread().empty());
    roll.Add(L"Technical Director, Chris Lamb");
    roll.Add(L"Production & Business Director, Duncan Kershaw");
    CHECK(roll.Unread() == Lines{L"Technical Director, Chris Lamb", L"Production & Business Director, Duncan Kershaw"});
    CHECK(roll.Count() == 5);
    roll.Clear();
    CHECK(roll.Count() == 0);
    CHECK(roll.Unread().empty());
}

TEST_CASE("credits: the roll keeps each line once: the same line twice in a row, or nothing, is not kept again")
{
    credits::Roll roll;
    roll.Add(L"Directors");
    roll.Add(L"Directors");
    roll.Add(L"  ");
    roll.Add(L"Game Director, Dan Lodge");
    roll.Add(L"Directors");
    CHECK(roll.Unread() == Lines{L"Directors", L"Game Director, Dan Lodge", L"Directors"});
}

TEST_CASE("credits: the roll's lines are taken once each, in the roll's order, as the polls find them, 2026-11-09")
{
    // The roll's start in research/sessions/s64-play.log (11:37:09.803 on): a line every half second,
    // gaps between the groups, the container listing every line still on the screen at each poll.
    credits::Taker taker;
    CHECK(taker.Take({{1, L"Ballistic Moon"}}, 0.0) == Lines{L"Ballistic Moon"});
    CHECK(taker.Taken(1));
    // A gap: it waits for words, the lines after it with it, so that the order stands.
    CHECK(taker.Take({{1, L"Ballistic Moon"}, {2, L""}}, 0.5).empty());
    CHECK(taker.Take({{1, L""}, {2, L""}, {3, L"Founders"}}, 1.0).empty());
    CHECK_FALSE(taker.Taken(2));
    // A second on, the gap is let go and what waited behind it is taken.
    CHECK(taker.Take({{1, L""}, {2, L""}, {3, L"Founders"}, {4, L"Creative Director, Neil McEwan"}}, 1.5) ==
          Lines{L"Founders", L"Creative Director, Neil McEwan"});
    CHECK(taker.Taken(2));
    // The lines already taken are not taken again, read or not.
    CHECK(taker.Take({{3, L"Founders"}, {4, L""}, {5, L"Technical Director, Chris Lamb"}}, 2.0) == Lines{L"Technical Director, Chris Lamb"});
    // A line whose words come a moment late is taken in its place, before the one after it.
    CHECK(taker.Take({{5, L""}, {6, L""}, {7, L"Directors"}}, 2.5).empty());
    CHECK(taker.Take({{6, L"Production & Business Director, Duncan Kershaw"}, {7, L"Directors"}}, 2.8) ==
          Lines{L"Production & Business Director, Duncan Kershaw", L"Directors"});
    taker.Clear();
    CHECK_FALSE(taker.Taken(1));
    CHECK(taker.Take({{1, L"Ballistic Moon"}}, 10.0) == Lines{L"Ballistic Moon"});
}

TEST_CASE("credits: the interview's overlay is its room as one line, then its clock; the tape's number is not said")
{
    CHECK(credits::OverlayWords(L"Полицейский участок\rКомната 2", L"09:32:24") == L"Полицейский участок, Комната 2. 09:32:24");
    CHECK(credits::OverlayWords(L"Police Interview\r\nRoom 1", L" 11:02:27 ") == L"Police Interview, Room 1. 11:02:27");
    CHECK(credits::OverlayWords(L"", L"09:32:24") == L"09:32:24");
    CHECK(credits::OverlayWords(L"Police Interview\rRoom 3", L"") == L"Police Interview, Room 3");
}

TEST_CASE("credits: the overlay says its room and clock as an interview begins, then the clock only as its hours or minutes change, 2026-11-09")
{
    // research/sessions/s64-play.log, 11:37:54.827 to 11:39:30.910: five interviews, the clock said
    // at each of its 87 ticks among the credits and the interviews' voices.
    credits::Overlay overlay;
    const std::wstring room2 = L"Полицейский участок\rКомната 2";
    CHECK(overlay.Update(true, room2, L"09:32:24", 54.8) == L"Полицейский участок, Комната 2. 09:32:24");
    CHECK(overlay.Update(true, room2, L"09:32:24", 54.9).empty());
    for (int s = 25; s <= 59; ++s)
        CHECK(overlay.Update(true, room2, L"09:32:" + std::to_wstring(s), 56.0 + s - 25).empty());
    // A minute on: the clock is said, its seconds as they stand.
    CHECK(overlay.Update(true, room2, L"09:33:00", 91.0) == L"09:33:00");
    CHECK(overlay.Update(true, room2, L"09:33:01", 92.0).empty());
    // Gone between two interviews; the next one is said whole.
    CHECK(overlay.Update(false, L"", L"", 93.0).empty());
    const std::wstring room3 = L"Полицейский участок\rКомната 3";
    CHECK(overlay.Update(true, room3, L"10:47:16", 94.0) == L"Полицейский участок, Комната 3. 10:47:16");
    CHECK(overlay.Update(true, room3, L"10:47:17", 95.0).empty());
    // Another room while it stays up (its texts set anew before it was seen hidden): said whole.
    const std::wstring room1 = L"Полицейский участок\rКомната 1";
    CHECK(overlay.Update(true, room1, L"11:02:27", 96.0) == L"Полицейский участок, Комната 1. 11:02:27");
    CHECK(overlay.Update(true, room1, L"11:02:28", 97.0).empty());
    // The same room again after it was hidden: a new interview, said whole.
    CHECK(overlay.Update(false, room1, L"11:02:28", 98.0).empty());
    CHECK(overlay.Update(true, room1, L"10:20:27", 99.0) == L"Полицейский участок, Комната 1. 10:20:27");
}

TEST_CASE("credits: an overlay shown before its texts are set says them once they come")
{
    credits::Overlay overlay;
    CHECK(overlay.Update(true, L"", L"", 0.0).empty());
    CHECK(overlay.Update(true, L"Police Interview\rRoom 2", L"09:50:03", 0.1) == L"Police Interview, Room 2. 09:50:03");
    CHECK(overlay.Update(true, L"Police Interview\rRoom 2", L"09:50:04", 1.1).empty());
}

namespace
{
    // The roll as the game runs it (UBatesCreditWidget::SpawnNextCredit, UBatesCreditLineWidget's tick):
    // a line rolls up at a steady pace, the next is spawned once the newest has moved its own height,
    // and a line past the top is taken off the container and its widget given back to the pool, to be
    // set up again for a line further on, in the same frame at worst. The roll is polled every `pollEvery`
    // frames; `newest` and `offsets` say whether the poll reads the roll's newest line and each line's
    // scroll. Returns the lines kept, in order, and the lines spawned.
    struct PooledRoll
    {
        std::vector<std::wstring> kept;
        std::vector<std::wstring> spawned;
    };

    PooledRoll RunPooledRoll(size_t lines, int pollEvery, bool newest, bool offsets, bool lifo)
    {
        struct Widget
        {
            uint64_t number = 0;
            int offset = 0;
            std::wstring words;
        };
        // In whole steps a frame (60 a second): a line rolls 2 a frame, the next is spawned once the newest
        // has rolled 60 (half a second), and a line is taken off at 2160 (18 s), in the very frame the
        // 37th line after it is spawned, which takes its widget.
        constexpr int kStep = 2;
        constexpr int kHeight = 60;
        constexpr int kFreeAt = 2160;
        std::vector<Widget> pool;      // free widgets
        std::vector<Widget> container; // on the screen, in spawn order
        uint64_t nextNumber = 1;
        uint64_t active = 0;
        PooledRoll out;
        credits::LineIds ids;
        credits::Taker taker;
        credits::Roll roll;
        for (int frame = 0; out.spawned.size() < lines || !container.empty(); ++frame)
        {
            const double now = frame / 60.0;
            for (auto& w : container)
                w.offset += kStep;
            // Past the top: off the container, back to the pool.
            for (auto it = container.begin(); it != container.end();)
            {
                if (it->offset >= kFreeAt)
                {
                    pool.push_back(*it);
                    it = container.erase(it);
                }
                else
                    ++it;
            }
            const auto newestLine = std::find_if(container.begin(), container.end(), [&](const Widget& w) { return w.number == active; });
            if (out.spawned.size() < lines && (active == 0 || newestLine == container.end() || newestLine->offset >= kHeight))
            {
                Widget w;
                if (!pool.empty())
                {
                    const auto taken = lifo ? pool.end() - 1 : pool.begin();
                    w = *taken;
                    pool.erase(taken);
                }
                else
                    w.number = nextNumber++;
                w.offset = 0;
                w.words = L"Line " + std::to_wstring(out.spawned.size() + 1);
                out.spawned.push_back(w.words);
                active = w.number;
                container.push_back(w);
            }
            if (frame % pollEvery != 0) continue;
            std::vector<credits::LineWidget> seen;
            for (const auto& w : container)
                seen.push_back(credits::LineWidget{w.number, offsets ? static_cast<double>(w.offset) : -1.0});
            const auto lineIds = ids.Update(seen, newest ? active : 0);
            std::vector<credits::Drawn> drawn;
            for (size_t i = 0; i < container.size(); ++i)
                drawn.push_back(credits::Drawn{lineIds[i], taker.Taken(lineIds[i]) ? std::wstring() : container[i].words});
            for (const auto& line : taker.Take(drawn, now))
                roll.Add(line);
        }
        out.kept = roll.Unread();
        return out;
    }
}

TEST_CASE("credits: the roll's pooled line widgets, set up again further on, show new lines: 37 widgets for the whole roll, 2026-11-09")
{
    // research/sessions/s64-play.log: 595 lines shown by 37 widgets (WBP_CreditLine_C_2147444060 at
    // 11:37:09.803, 11:37:43.807, 11:38:33.861, ...); known by its widget, a line was kept the first
    // time only, and the roll stopped after its first 37 lines.
    for (const bool lifo : {false, true})
    {
        // Polled every third frame, the newest line and each line's scroll read.
        auto run = RunPooledRoll(1200, 3, true, true, lifo);
        CHECK(run.kept.size() == 1200);
        CHECK(run.kept == run.spawned);
        // Either sign alone keeps every line.
        run = RunPooledRoll(1200, 3, true, false, lifo);
        CHECK(run.kept == run.spawned);
        run = RunPooledRoll(1200, 3, false, true, lifo);
        CHECK(run.kept == run.spawned);
        // A hitch: polls two spawns apart; the scroll gone back still tells a widget set up again.
        run = RunPooledRoll(400, 40, true, true, lifo);
        CHECK(run.kept == run.spawned);
        // With neither sign, a widget freed and set up again between two polls is the old line.
        run = RunPooledRoll(400, 3, false, false, lifo);
        CHECK(run.kept.size() < run.spawned.size());
    }
}

TEST_CASE("credits: a line widget's line stands while it stays on the container, and is new once it was off it")
{
    credits::LineIds ids;
    const auto first = ids.Update({{1, 0.0}, {2, 0.0}}, 2);
    CHECK(first.size() == 2);
    CHECK(first[0] != first[1]);
    // Rolling on: the same lines.
    CHECK(ids.Update({{1, 30.0}, {2, 20.0}}, 2) == first);
    // Widget 1 off the container, then back further on: a new line.
    CHECK(ids.Update({{2, 40.0}}, 2) == std::vector<uint64_t>{first[1]});
    const auto again = ids.Update({{2, 60.0}, {1, 0.0}}, 1);
    CHECK(again[0] == first[1]);
    CHECK(again[1] != first[0]);
    // Its scroll unknown and the newest line not read, a widget never seen off is the same line.
    CHECK(ids.Update({{2, -1.0}, {1, -1.0}}, 0) == again);
    ids.Clear();
    CHECK(ids.Update({{2, 0.0}}, 2)[0] != again[0]);
    // With neither the newest line nor the scroll read, a widget seen off the container is new when it
    // comes back.
    credits::LineIds bare;
    const auto a = bare.Update({{1, -1.0}, {2, -1.0}}, 0);
    CHECK(bare.Update({{2, -1.0}}, 0) == std::vector<uint64_t>{a[1]});
    const auto b = bare.Update({{2, -1.0}, {1, -1.0}}, 0);
    CHECK(b[0] == a[1]);
    CHECK(b[1] != a[0]);
}
