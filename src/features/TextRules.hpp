#pragma once
// Which lines of the texts the game draws in play are new, worked out without the game so that it
// can be tested: subtitles as they appear, and the lines of the HUD's elements.

#include <map>
#include <set>
#include <string>
#include <vector>

namespace da::lines
{
    // Subtitles, every frame, so that a line is read the moment it appears: the lines on screen
    // are compared with those of the two polls before, and only new text is spoken. Two new lines
    // in one frame are one subtitle wrapped over two lines, or two speakers at once; they come
    // back together.
    class Subtitles
    {
    public:
        // The lines on screen now, in order; the new ones.
        std::vector<std::wstring> Update(const std::vector<std::wstring>& shown);
        // The lines on screen at the last poll.
        const std::vector<std::wstring>& Shown() const
        {
            return m_shown;
        }
        void Reset()
        {
            m_shown.clear();
            m_before.clear();
        }

    private:
        std::vector<std::wstring> m_shown;  // at the last poll
        std::vector<std::wstring> m_before; // and at the poll before that
    };

    // A HUD element is read once its lines have stood for kStablePolls polls: a caption is filled
    // in a moment after the element appears, and the same element can show several messages in a
    // row. Only the lines that are new are said: a line that stays while others come and go (a
    // title over a sequence of sentences) is said once; a line that went away and comes back is
    // new again, unless it is back within kLineForgotten (it blinked). A clock that ticks on is
    // said at every tick, as it shows each. Hidden and shown again, the element is read again.
    // An element laid out but drawn at no opacity shows no lines, so they are new again once it
    // is drawn after kLineForgotten: a document's transcript hidden with its key fades the
    // reading widget out and leaves it on the screen's list (2026-11-01, the wanted poster's
    // transcript shown again and not read).
    constexpr int kStablePolls = 2;
    constexpr double kLineForgotten = 2.0;
    class Element
    {
    public:
        // A poll of the element shown with `lines`, `drawn` unless it is drawn at no opacity:
        // the lines to say now.
        std::vector<std::wstring> Shown(const std::vector<std::wstring>& lines, double now, bool drawn = true);
        void Hidden();
        // Some of `lines` are still to be said: new, or said longer than kLineForgotten ago.
        bool Unsaid(const std::vector<std::wstring>& lines, double now) const;
        // `lines`, given by Shown, were not said after all: Shown gives them again at the next poll.
        void Unsay(const std::vector<std::wstring>& lines);
        // Its lines at the last poll.
        const std::vector<std::wstring>& Seen() const
        {
            return m_seen;
        }

    private:
        std::vector<std::wstring> m_seen;
        std::map<std::wstring, double> m_spoken; // the lines said, by the time each was last on it
        int m_stablePolls = 0;
    };

    // An update card (the butterfly effect's, a totem's, a clue's: a title, a body and a key)
    // draws its title and key first and fades its body, the name of what was found, in a second or
    // two later; read as it came, it was said twice, the second time with the name ("Эффект
    // бабочки: R1", then "Эффект бабочки. Хранитель секретов. R1", 2026-11-02). A card waits for
    // its body to be drawn, for at most kCardWait, and is then read whole.
    constexpr double kCardWait = 3.0;
    class Card
    {
    public:
        // A poll of the card, its body drawn or not: whether its lines are read now.
        bool Ready(double now, bool bodyDrawn);
        void Hidden()
        {
            m_since = -1.0;
        }
        // How long the card has been up, as of its first poll; 0 before it.
        double ShownFor(double now) const
        {
            return m_since < 0.0 ? 0.0 : now - m_since;
        }

    private:
        double m_since = -1.0; // since when the card has been shown
    };

    // A title card (a chapter's, a character's) lays its words out at once and fades them in one
    // after another: the card that changes to Mike at chapter 9 drew "МАЙК" 0.3 s after it came up,
    // "Лечебница" at 1.1 s and "3:03" at 1.8 s, and the bolt door's prompt, up at 1.4 s, was said
    // between the place and the time (research/sessions/s62-play.log, 21:33:37.165 to 38.961).
    // While a word it lays out has not been drawn yet, the card is still filling, for at most
    // kTitleFillMost from its coming up; the prompts wait for it, as they wait for a hint.
    constexpr double kTitleFillMost = 3.0;
    class TitleFill
    {
    public:
        // A poll of the card shown, with the texts it lays out (`laidOut`, faded in or not) and those
        // it draws now: whether it is still filling.
        bool Update(double now, const std::vector<std::wstring>& laidOut, const std::vector<std::wstring>& drawn);
        void Hidden();

    private:
        double m_since = -1.0;
        std::set<std::wstring> m_drawn; // every text it has drawn since it came up
    };

    // A document read page by page (a book turned with the stick, its transcript shown a page at
    // a time): a page's text put in place of the one before interrupts what is still being said
    // of that one, which is no longer on the screen; the first transcript of a reading waits its
    // turn after what is being said. A reading lasts while the character is held by its
    // interaction and ends when she is handed back. Turning the cabin's books back and forth on
    // 2026-11-06 queued page after page behind one another (Andrey asked that a new page cut the
    // old one short as its text comes, not as the stick moves).
    class Reading
    {
    public:
        // A transcript's text is to be said now: whether it cuts short what is being said.
        bool Interrupts()
        {
            const bool turned = m_open;
            m_open = true;
            return turned;
        }
        void Ended()
        {
            m_open = false;
        }
        // A reading is open: its first page has been said, and she is still held by it.
        bool Open() const
        {
            return m_open;
        }

    private:
        bool m_open = false;
    };

    // A clue's card and the page of a document it comes with are said together, the card first: a
    // book's page that holds a clue shows the clue's card as the page comes up, and which of the two
    // was said first went by which the game had drawn first. Turning Jack's journal in act7_1, the
    // card came before its page twice and after it twice, once behind the prompts read with the page
    // (research/sessions/s61-play.log, 18:59:39 to 19:03:01), and Andrey asked for one order. So a
    // page waits while a card shown with it is still to be read (its name not drawn yet), and a card
    // while the page of the reading open is still to come (its transcript hidden as the page turns,
    // or not read yet), each for at most kCardWithPage; ready together, they are said as one. A page
    // with no card shown waits for nothing.
    constexpr double kCardWithPage = 1.5;
    // A card stands about four seconds; one up this long already is said at once, lest it go unsaid.
    constexpr double kCardLongest = 2.5;
    class CardFirst
    {
    public:
        // Whether a page ready to be said waits this poll, a card still coming (`cardComing`).
        bool HoldPage(double now, bool cardComing);
        // Whether a card ready to be said waits this poll, a page still coming (`pageComing`), the card
        // up for `shownFor` seconds.
        bool HoldCard(double now, bool pageComing, double shownFor = 0.0);
        // Nothing waits this poll: the next wait begins afresh.
        void Settled();
        // Whether a page or a card waits, as of `now`: from a poll that held one until the next poll's
        // word, lapsing by itself kHoldLapse after it, lest a poll that never comes (the HUD stopped,
        // the mod switched off) hold the prompts behind it for good.
        bool Holding(double now) const;

    private:
        double m_pageSince = -1.0;
        double m_cardSince = -1.0;
        double m_holdUntil = -1.0;
    };
    // The HUD polls every sixth frame: 0.1 s at 60 frames a second, 0.4 s at 15.
    constexpr double kHoldLapse = 0.5;

    // A page still to come for a card to wait for: the reading's transcript not yet said, or hidden as
    // its page turns, no longer than a turn hides it (Jack's journal: 1.3 s a turn, 3 s for Ashley's
    // comments). Hidden longer, the reading is over though she is not yet handed back, and a card waits
    // for nothing.
    constexpr double kPageTurnGap = 3.0;
    bool PageComing(bool readingOpen, bool shown, double hiddenFor, bool unsaid);

    // A document's page shown again unchanged is left to be read on only while the reader may still be
    // on it (`stillOn`, speech::StillOn) or the same words were let through moments ago (`echoed`,
    // lines::Echo), and the player has done nothing since it was hidden: the game hid it itself
    // (Ashley's comments on Jack's journal, three seconds each). Shown again after the player's input
    // (the transcript's key pressed twice, a key that silenced the reader, a push of the stick), it is
    // said again, as a transcript shown again always was.
    bool LeaveShownAgain(bool stillOn, bool echoed, bool inputSinceHidden);

    // What is said of a document's page ready to be said, with the cards that came with it: the cards
    // first, then the page; of a page left to be read on (LeaveShownAgain), only the cards, said
    // behind it.
    std::vector<std::wstring> PageWords(const std::vector<std::wstring>& cards, const std::wstring& page, bool leave);

    // The same words can arrive through two elements at once: said once within kSameWords.
    constexpr double kSameWords = 5.0;
    class Echo
    {
    public:
        bool Says(const std::wstring& text, double now);

    private:
        std::wstring m_last;
        double m_at = -1.0;
    };

    // The objective the game keeps for the player, shown on its status page, is said when the
    // scene's script sets another; but a chapter's script sets its first objective while the card
    // that names the chapter is still fading in, and the game draws the objective nowhere then.
    // So an objective waits until play runs with no title card up, and has run so for kFreeFor
    // (a card can be found a moment after it went up); then it comes after the card's words. A
    // newer objective set meanwhile takes its place; one turned off is not said.
    constexpr double kFreeFor = 1.0;
    //
    // An objective belongs to the character whose turn it is, and the card that names her (the
    // character change card: name, place, time) comes some way into the scene that opens her
    // turn: at the lodge on 2026-11-05 Sam's objective was set as the level loaded and said 19 s
    // before "SAM, Washington Lodge, 20:44", Matt's 3 s before his; at chapter 2 Jessica's came
    // 0.7 s before her card. So from a level's load, or the player's being given another character
    // (Opening), a new objective waits for that card to have come and gone (Card), or, where no
    // card comes, for the player's first hand-over (HandedOver).
    class Objective
    {
    public:
        struct Step
        {
            bool changed = false; // the game set another objective, or turned it off, since the last poll
            std::wstring say;     // the words to say now, if any
        };
        // A poll: the objective the game keeps now, and whether play runs with no title card up.
        Step Update(double now, const std::wstring& objective, bool free);
        // A level loads, or the player is given another character.
        void Opening()
        {
            m_opening = true;
            m_cardSeen = false;
        }
        // The card that changes the character is up.
        void Card()
        {
            if (m_opening) m_cardSeen = true;
        }
        // The player is handed the character.
        void HandedOver()
        {
            m_opening = false;
        }
        bool Opens() const
        {
            return m_opening;
        }
        const std::wstring& Current() const
        {
            return m_current;
        }
        bool Known() const
        {
            return m_known;
        }
        // An objective set but not said yet.
        bool Due() const
        {
            return m_due;
        }

    private:
        std::wstring m_current;
        bool m_known = false;
        bool m_due = false;
        double m_freeSince = -1.0;
        bool m_opening = false;
        bool m_cardSeen = false;
    };
}
