#pragma once
// The epilogue's credits as the mod keeps them, worked out without the game so that it can be
// tested. The game rolls its credits (UBatesCreditWidget: a line every half second, each a row of
// entries side by side, a role and a name, or two names) over the survivors' police interviews and
// the films of the dead; read as they rolled, every line was said up to four times (the roll, its
// line, the line's role, its name) and the interviews were lost among them
// (research/sessions/s64-play.log, 11:37:09 to 11:49:22: 2516 lines said in twelve minutes). So the
// credits are kept, each line once in its order, and read only on the player's key: what came since
// the last reading, all of it the first time (Andrey's request).

#include "features/TextRules.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace da::credits
{
    // A line of the roll as words: its entries' texts side by side, each trimmed, the empty ones (the
    // left column of a name that goes on a list, a gap) left out, joined by commas. Empty for a line
    // with no words.
    std::wstring LineOf(const std::vector<std::wstring>& entries);

    class Roll
    {
    public:
        // A line the roll drew: kept unless empty or the line kept last.
        void Add(const std::wstring& line);
        // The lines since the last reading, from the first the first time; they are read now.
        std::vector<std::wstring> Unread();
        bool AnyUnread() const
        {
            return m_read < m_lines.size();
        }
        size_t Count() const
        {
            return m_lines.size();
        }
        // A new roll begins.
        void Clear();

    private:
        std::vector<std::wstring> m_lines;
        size_t m_read = 0;
    };

    // The roll's line widgets are pooled (UBMUIUserWidgetPool): a line scrolled off the top is taken
    // off the container and the same widget, its address and its name unchanged, is set up again for
    // a line further on (UBatesCreditWidget::SpawnNextCredit takes it from the pool, makes it the
    // ActiveCreditLine, sets it up and adds it to the container in one call). 37 widgets showed the
    // whole roll, 595 times (research/sessions/s64-play.log), and a line known by its widget alone
    // was kept the first time only: the roll stopped after its first 37 lines. A line is a widget's
    // setting up: the widget the roll made its newest line since the last poll, or one off the
    // container at the last poll, or one whose scroll went back (Setup puts a line's CurrentOffset back
    // to 0, and it only grows as the line rolls up), is a new line.
    struct LineWidget
    {
        uint64_t widget = 0;  // a number of the widget's own
        double offset = -1.0; // how far it has rolled (CurrentOffset), or -1 where not known
    };
    class LineIds
    {
    public:
        // A poll: the widgets on the container in its order, and the roll's newest line's widget (0 for
        // none). The line of each widget, in the same order.
        std::vector<uint64_t> Update(const std::vector<LineWidget>& widgets, uint64_t newest);
        void Clear();

    private:
        struct Known
        {
            uint64_t line = 0;
            double offset = -1.0;
        };
        std::map<uint64_t, Known> m_known; // by widget
        uint64_t m_newest = 0;             // the newest line's widget at the last poll
        uint64_t m_next = 1;
    };

    // The lines on the roll's container at a poll, in the container's order (the order the roll drew
    // them in): each by an identity of its own and its words (LineOf, empty for none).
    struct Drawn
    {
        uint64_t id = 0;
        std::wstring words;
    };
    // A line found with no words waits this long for them before it is taken for a gap between the
    // roll's groups (the data's empty lines); the lines drawn after it wait with it, so that the order
    // stands.
    constexpr double kWordsWait = 1.0;
    class Taker
    {
    public:
        // A poll's lines: those to keep now, in their order, each once.
        std::vector<std::wstring> Take(const std::vector<Drawn>& lines, double now);
        // A line kept already, or let go as a gap: its words need not be read again.
        bool Taken(uint64_t id) const;
        void Clear();

    private:
        std::set<uint64_t> m_taken;
        std::map<uint64_t, double> m_waiting; // lines with no words yet, since when
    };

    // The police station's overlay over an interview (UBatesCreditCCTVWidget: the room, the tape's
    // number and the clock), worded: the room's two lines ("Полицейский участок\rКомната 2") as one,
    // then the clock; the tape's number is read with the screen only.
    std::wstring OverlayWords(const std::wstring& room, const std::wstring& time);

    // The overlay as an interview runs: said as it comes up, or as another room comes, room and clock;
    // after that the clock as its hours or minutes change (lines::ClockTicks): it ticks every second
    // and was said 87 times over the interviews.
    class Overlay
    {
    public:
        // A poll: whether the overlay is shown, and its room's and clock's texts. The words to say now,
        // empty for none.
        std::wstring Update(bool shown, const std::wstring& room, const std::wstring& time, double now);

    private:
        bool m_shown = false;
        std::wstring m_room;
        lines::ClockTicks m_clock;
    };
}
