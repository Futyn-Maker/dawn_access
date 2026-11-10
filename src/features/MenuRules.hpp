#pragma once
// The menus worked out without the game, so that they can be tested: when a screen the player
// arrives at is read, how a value that moves under a held key is paced, when a change of the
// prompts is said, and a text's shape, which tells a count that ticks from new words.

#include <set>
#include <string>
#include <vector>

namespace da::menu
{
    // An arrival waits for the page to settle: the focus and the texts land a few frames after the
    // page is displayed. The page is read once its focus has landed and its stack displays it,
    // kSettle after it came (the focus can be given to a page a few frames before it is drawn, as
    // the collectibles' first tab is), or at kSettleMax whatever.
    constexpr double kSettle = 0.3;
    constexpr double kSettleMax = 1.2;

    class Arrival
    {
    public:
        // A page arrives, or the same one is shown anew: true when the wait begins now, false while
        // it already waits.
        bool Begin(double now, bool anew);
        bool Pending() const
        {
            return m_pending;
        }
        // Whether the page is read now. `ready` says whether its focus has landed and its stack
        // displays it, and is asked only once kSettle has passed; `anew` gets whether the page is
        // the same one shown anew.
        template <typename Ready>
        bool Due(double now, Ready ready, bool& anew)
        {
            const bool settled = now - m_at >= kSettle && ready();
            if (!settled && now - m_at < kSettleMax) return false;
            anew = m_anew;
            m_pending = false;
            m_anew = false;
            return true;
        }
        // The page on display is the one read: no arrival waits.
        void Clear()
        {
            m_pending = false;
            m_anew = false;
        }

    private:
        bool m_pending = false;
        bool m_anew = false; // the page waited for is the same page shown anew, not a page the player came to
        double m_at = 0.0;
    };

    // A value that moves under a held key is said at most every kValueApart, and once more as it
    // comes to rest, kValueRest after its last change; each saying interrupts the one before.
    constexpr double kValueApart = 0.35;
    constexpr double kValueRest = 0.15;

    class Value
    {
    public:
        // The value as it was said with the control.
        void Known(const std::wstring& value)
        {
            m_value = value;
            m_due = false;
        }
        // No control is read any more.
        void Forget()
        {
            m_value.clear();
        }
        // A poll of the focused control's value: the words to say now, or nothing.
        std::wstring Update(double now, const std::wstring& value);

    private:
        std::wstring m_value; // the value last seen
        bool m_due = false;   // the value seen has not been said yet
        double m_changedAt = 0.0;
        double m_spokenAt = 0.0;
    };

    // A change of the prompts is said once it has stood kPromptStable.
    constexpr double kPromptStable = 0.5;

    class Steady
    {
    public:
        // A poll, the prompts changed or not since they were said: true when the change has stood.
        bool Stood(double now, bool changed);
        void Reset()
        {
            m_since = -1.0;
        }

    private:
        double m_since = -1.0;
    };

    // A text with its digits taken out, so that a count that ticks is one text.
    std::wstring Shape(const std::wstring& text);

    // The page's texts said with a control the focus moved to: where it came from another group of
    // controls (`newGroup`), the page's texts not said on it yet (`said`, by Shape), in their order,
    // go before the control; moving within a group says the control alone. The chapter select opens a
    // question of its own with its two buttons, "Начать?" over "Да" and "Нет", and only "Да" was said
    // (research/sessions/s65-play.log, 14:30:53.489): the page's texts were taken for said as the focus
    // moved, the question with them.
    std::vector<std::wstring> TextsWithFocus(bool newGroup, const std::vector<std::wstring>& body, const std::set<std::wstring>& said);

    // The game's own narration of a control is one utterance queued as lines: the first clears
    // what its priority had queued (bInClearPrio), the others join it in the same frame. A totem
    // slot hovered queues "Тотем смерти", "6", "Недоступно" (WBP_TotemSegment's hover), and the
    // mod took the last line alone, so a slot said "Недоступно" and a totem found "1"
    // (2026-11-01). The control's narration is every line of its utterance; a line that clears,
    // or one queued at another frame, begins a new one.
    class Utterance
    {
    public:
        void Queued(const std::wstring& line, bool clears, unsigned long long frame);
        std::wstring Line() const;
        // The line that clears, the one that names the control, was empty: the game left the name
        // out. A slot of the hunger totems queues "" and its place where the other columns' slots
        // queue "Тотем удачи" and theirs: the game has a name for no hunger totem's kind but its
        // column's caption, "Голод" (research/sessions/s62-play.log, 21:39:09).
        bool Nameless() const
        {
            return m_nameless;
        }

    private:
        std::vector<std::wstring> m_lines;
        unsigned long long m_frame = 0;
        bool m_nameless = false;
    };

    // The words of a control the game's narration left nameless (Utterance::Nameless): the heading
    // drawn over the control's group names it, as the narration of its neighbours names theirs.
    std::wstring NamedByHeading(const std::wstring& heading, const std::wstring& label);

    // A text a control shows, and whether it sits in the badge the game sets on an item the
    // player has not looked at yet (BatesUnseenItemWidget: "НОВОЕ" on a butterfly, a clue, a
    // totem found).
    struct Shown
    {
        std::wstring text;
        bool badge = false;
    };

    // Whether a control's texts name it: a badge is a mark on the item, not its name, and a totem
    // found draws its badge and nothing else.
    bool Names(const std::vector<Shown>& texts);

    // A control's words: the texts that name it, else `otherwise` (what the game's narration
    // says of it, the picture table's words), and then its marks.
    std::wstring Named(const std::vector<Shown>& texts, const std::wstring& otherwise);

    // The controls a panel holds as its children: whether there are any, and whether any of
    // them is named.
    struct Controls
    {
        bool any = false;
        bool named = false;
    };

    // Whether a panel lays out controls, so that its other texts head them and are said with
    // them rather than as the page's own words: it holds controls itself, or a panel among its
    // children holds controls none of which is named (a row of pictures). A text beside a row
    // of named buttons is the page's own, such as the question of a modal. A totem found named
    // its column by its badge, and the columns' captions were read as the page's text
    // (2026-11-01: "Тотемы. Смерть. Наставление.").
    bool LaysOutControls(const Controls& own, const std::vector<Controls>& panels);

    // Whether the texts beside a row of controls, in the panel that holds the row, ask the question
    // the row answers: a row of named buttons (`row`) in a panel that lays out no controls itself
    // (`own`, `panels`: its own controls and its child panels', LaysOutControls). Such a text is the
    // page's own, but the page's walk can leave it out with the panel around it: the chapter select's
    // root also holds the carousel's two arrows, wordless buttons, so the whole page lays out controls
    // and its texts are no body; its "Начать?" over "Да" and "Нет" was never said
    // (research/sessions/s66-play.log, 16:05:28.737), though the focus came to "Да" from another group.
    bool AsksOver(const Controls& row, const Controls& own, const std::vector<Controls>& panels);

    // The page's texts with a question added (AsksOver), each text once by its Shape, the page's
    // first: what TextsWithFocus weighs as the focus comes to the row.
    std::vector<std::wstring> WithQuestion(std::vector<std::wstring> body, const std::vector<std::wstring>& question);
}
