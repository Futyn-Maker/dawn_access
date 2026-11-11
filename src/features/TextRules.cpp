#include "features/TextRules.hpp"

#include "core/Strings.hpp"

#include <algorithm>
#include <cwctype>
#include <set>

namespace da::lines
{
    namespace
    {
        bool Contains(const std::vector<std::wstring>& lines, const std::wstring& text)
        {
            return std::find(lines.begin(), lines.end(), text) != lines.end();
        }
    }

    std::vector<std::wstring> Subtitles::Update(const std::vector<std::wstring>& shown)
    {
        std::vector<std::wstring> fresh;
        for (const auto& text : shown)
        {
            if (!Contains(m_shown, text) && !Contains(m_before, text) && !Contains(fresh, text)) fresh.push_back(text);
        }
        m_before = std::move(m_shown);
        m_shown = shown;
        return fresh;
    }

    bool OnScreenText(const std::wstring& line, const std::vector<std::wstring>& onScreen)
    {
        const std::wstring words = str::CollapseWhitespace(line);
        if (words.empty()) return false;
        return std::any_of(onScreen.begin(), onScreen.end(), [&](const std::wstring& text) { return str::CollapseWhitespace(text) == words; });
    }

    std::vector<std::wstring> Element::Shown(const std::vector<std::wstring>& drawnLines, double now, bool drawn)
    {
        static const std::vector<std::wstring> kNone;
        const auto& lines = drawn ? drawnLines : kNone;
        if (lines != m_seen)
        {
            m_seen = lines;
            m_stablePolls = 0;
            return {};
        }
        if (++m_stablePolls < kStablePolls) return {};
        std::vector<std::wstring> fresh;
        std::set<std::wstring> present;
        for (const auto& line : lines)
        {
            if (!present.insert(line).second) continue;
            const auto known = m_spoken.find(line);
            if (known == m_spoken.end() || now - known->second > kLineForgotten) fresh.push_back(line);
        }
        for (const auto& line : present)
            m_spoken[line] = now;
        std::erase_if(m_spoken, [&](const auto& entry) { return now - entry.second > kLineForgotten; });
        return fresh;
    }

    void Element::Hidden()
    {
        m_spoken.clear();
        m_seen.clear();
    }

    bool Element::Unsaid(const std::vector<std::wstring>& lines, double now) const
    {
        return std::any_of(lines.begin(), lines.end(),
                           [&](const std::wstring& line)
                           {
                               const auto known = m_spoken.find(line);
                               return known == m_spoken.end() || now - known->second > kLineForgotten;
                           });
    }

    void Element::Unsay(const std::vector<std::wstring>& lines)
    {
        for (const auto& line : lines)
            m_spoken.erase(line);
    }

    bool IsClock(const std::wstring& line, std::vector<int>* fields)
    {
        std::vector<int> parts;
        size_t i = 0;
        while (true)
        {
            const size_t start = i;
            int value = 0;
            while (i < line.size() && line[i] >= L'0' && line[i] <= L'9')
            {
                value = value * 10 + (line[i] - L'0');
                ++i;
            }
            const size_t digits = i - start;
            // The first field of one or two digits, the others of two.
            if (digits == 0 || digits > 2 || (!parts.empty() && digits != 2)) return false;
            parts.push_back(value);
            if (i == line.size()) break;
            if (line[i] != L':') return false;
            ++i;
        }
        if (parts.size() < 2 || parts.size() > 3) return false;
        if (fields) *fields = std::move(parts);
        return true;
    }

    std::vector<std::wstring> ClockTicks::Filter(const std::vector<std::wstring>& fresh, double now)
    {
        std::vector<std::wstring> out;
        for (const auto& line : fresh)
        {
            std::vector<int> fields;
            if (!IsClock(line, &fields))
            {
                out.push_back(line);
                continue;
            }
            bool say = false;
            if (m_last.empty() || m_last.size() != fields.size())
                say = true;
            else if (fields.size() == 3)
                say = fields[0] != m_last[0] || fields[1] != m_last[1];
            else
                say = fields[0] != m_last[0] || (fields != m_last && now - m_changedAt >= kClockStill);
            if (fields != m_last) m_changedAt = now;
            m_last = std::move(fields);
            if (say) out.push_back(line);
        }
        return out;
    }

    void ClockTicks::Hidden()
    {
        m_last.clear();
        m_changedAt = -1.0;
    }

    namespace
    {
        bool Holds(double& since, double now, bool other)
        {
            if (!other)
            {
                since = -1.0;
                return false;
            }
            if (since < 0.0) since = now;
            return now - since < kCardWithPage;
        }
    }

    bool CardFirst::HoldPage(double now, bool cardComing)
    {
        const bool hold = Holds(m_pageSince, now, cardComing);
        if (hold) m_holdUntil = now + kHoldLapse;
        return hold;
    }

    bool CardFirst::HoldCard(double now, bool pageComing, double shownFor)
    {
        const bool hold = Holds(m_cardSince, now, pageComing && shownFor < kCardLongest);
        if (hold) m_holdUntil = now + kHoldLapse;
        return hold;
    }

    bool CardFirst::Holding(double now) const
    {
        return now < m_holdUntil;
    }

    bool PageComing(bool readingOpen, bool shown, double hiddenFor, bool unsaid)
    {
        if (!readingOpen) return false;
        return shown ? unsaid : hiddenFor < kPageTurnGap;
    }

    bool LeaveShownAgain(bool stillOn, bool echoed, bool inputSinceHidden)
    {
        return (stillOn || echoed) && !inputSinceHidden;
    }

    std::vector<std::wstring> PageWords(const std::vector<std::wstring>& cards, const std::wstring& page, bool leave)
    {
        std::vector<std::wstring> words = cards;
        if (!leave) words.push_back(page);
        return words;
    }

    void CardFirst::Settled()
    {
        m_pageSince = -1.0;
        m_cardSince = -1.0;
        m_holdUntil = -1.0;
    }

    bool TitleFill::Update(double now, const std::vector<std::wstring>& laidOut, const std::vector<std::wstring>& drawn)
    {
        if (m_since < 0.0) m_since = now;
        m_drawn.insert(drawn.begin(), drawn.end());
        if (now - m_since >= kTitleFillMost) return false;
        return std::any_of(laidOut.begin(), laidOut.end(), [&](const std::wstring& text) { return !m_drawn.count(text); });
    }

    void TitleFill::Hidden()
    {
        m_since = -1.0;
        m_drawn.clear();
    }

    bool Card::Ready(double now, bool bodyDrawn)
    {
        if (m_since < 0.0) m_since = now;
        return bodyDrawn || now - m_since >= kCardWait;
    }

    bool Echo::Says(const std::wstring& text, double now)
    {
        if (text == m_last && now - m_at <= kSameWords) return false;
        m_last = text;
        m_at = now;
        return true;
    }

    Objective::Step Objective::Update(double now, const std::wstring& objective, bool free)
    {
        Step step;
        if (!m_known || objective != m_current)
        {
            m_known = true;
            m_current = objective;
            m_due = !objective.empty();
            step.changed = true;
        }
        // The opening of a turn ends once its card has come and gone.
        if (m_opening && m_cardSeen) m_opening = false;
        if (!free || m_opening)
        {
            m_freeSince = -1.0;
            return step;
        }
        if (m_freeSince < 0.0) m_freeSince = now;
        if (!m_due || now - m_freeSince < kFreeFor) return step;
        m_due = false;
        step.say = m_current;
        return step;
    }

    bool NamesBareKey(const std::wstring& line)
    {
        return std::any_of(line.begin(), line.end(), [](wchar_t ch) { return std::iswalpha(ch) != 0; });
    }

    bool FurnitureOnly(const std::wstring& widgetClass)
    {
        return widgetClass == L"WBP_CameraOverlay_C";
    }
}
