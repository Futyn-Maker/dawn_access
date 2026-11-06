#pragma once
// Which lines of the texts the game draws in play are new, worked out without the game so that it
// can be tested: subtitles as they appear, and the lines of the HUD's elements.

#include <map>
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

    private:
        double m_since = -1.0; // since when the card has been shown
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

    private:
        bool m_open = false;
    };

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
