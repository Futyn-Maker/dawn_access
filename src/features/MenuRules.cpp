#include "features/MenuRules.hpp"

#include "core/Strings.hpp"

#include <algorithm>
#include <cwctype>

namespace da::menu
{
    bool Arrival::Begin(double now, bool anew)
    {
        if (m_pending) return false;
        m_pending = true;
        m_anew = anew;
        m_at = now;
        return true;
    }

    std::wstring Value::Update(double now, const std::wstring& value)
    {
        if (value != m_value)
        {
            m_value = value;
            m_changedAt = now;
            m_due = !value.empty();
        }
        if (!m_due || (now - m_spokenAt < kValueApart && now - m_changedAt < kValueRest)) return {};
        m_due = false;
        m_spokenAt = now;
        return m_value;
    }

    bool Steady::Stood(double now, bool changed)
    {
        if (!changed)
        {
            m_since = -1.0;
            return false;
        }
        if (m_since < 0.0) m_since = now;
        if (now - m_since < kPromptStable) return false;
        m_since = -1.0;
        return true;
    }

    std::wstring Shape(const std::wstring& text)
    {
        std::wstring shape;
        for (const wchar_t c : text)
        {
            if (!iswdigit(c)) shape += c;
        }
        return shape;
    }

    void Utterance::Queued(const std::wstring& line, bool clears, unsigned long long frame)
    {
        if (line.empty()) return;
        if (clears || frame != m_frame) m_lines.clear();
        m_frame = frame;
        m_lines.push_back(line);
    }

    std::wstring Utterance::Line() const
    {
        return str::Join(m_lines, L", ");
    }

    bool Names(const std::vector<Shown>& texts)
    {
        return std::any_of(texts.begin(), texts.end(), [](const Shown& t) { return !t.badge; });
    }

    std::wstring Named(const std::vector<Shown>& texts, const std::wstring& otherwise)
    {
        std::vector<std::wstring> words;
        for (const auto& t : texts)
        {
            if (!t.badge) words.push_back(t.text);
        }
        if (words.empty() && !otherwise.empty()) words.push_back(otherwise);
        for (const auto& t : texts)
        {
            if (t.badge) words.push_back(t.text);
        }
        return str::Join(words, L", ");
    }

    bool LaysOutControls(const Controls& own, const std::vector<Controls>& panels)
    {
        return own.any || std::any_of(panels.begin(), panels.end(), [](const Controls& p) { return p.any && !p.named; });
    }
}
