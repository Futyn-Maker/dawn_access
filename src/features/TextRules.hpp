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
    constexpr int kStablePolls = 2;
    constexpr double kLineForgotten = 2.0;
    class Element
    {
    public:
        // A poll of the element shown with `lines`: the lines to say now.
        std::vector<std::wstring> Shown(const std::vector<std::wstring>& lines, double now);
        void Hidden();
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
}
