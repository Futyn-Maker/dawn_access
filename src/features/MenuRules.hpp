#pragma once
// The menus worked out without the game, so that they can be tested: when a screen the player
// arrives at is read, how a value that moves under a held key is paced, when a change of the
// prompts is said, and a text's shape, which tells a count that ticks from new words.

#include <string>

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
}
