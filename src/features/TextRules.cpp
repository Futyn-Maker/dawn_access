#include "features/TextRules.hpp"

#include <algorithm>
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

    std::vector<std::wstring> Element::Shown(const std::vector<std::wstring>& lines, double now)
    {
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

    bool Echo::Says(const std::wstring& text, double now)
    {
        if (text == m_last && now - m_at <= kSameWords) return false;
        m_last = text;
        m_at = now;
        return true;
    }
}
