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

    std::vector<std::wstring> TextsWithFocus(bool newGroup, const std::vector<std::wstring>& body, const std::set<std::wstring>& said)
    {
        std::vector<std::wstring> out;
        if (!newGroup) return out;
        for (const auto& text : body)
        {
            if (!said.count(Shape(text))) out.push_back(text);
        }
        return out;
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
        // An empty line adds nothing, but one that clears begins the utterance all the same, its
        // name left out.
        if (line.empty() && !clears) return;
        if (clears || frame != m_frame)
        {
            m_lines.clear();
            m_nameless = clears && line.empty();
        }
        m_frame = frame;
        if (!line.empty()) m_lines.push_back(line);
    }

    std::wstring NamedByHeading(const std::wstring& heading, const std::wstring& label)
    {
        if (heading.empty()) return label;
        if (label.empty()) return heading;
        return heading + L", " + label;
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

    bool AsksOver(const Controls& row, const Controls& own, const std::vector<Controls>& panels)
    {
        return row.any && row.named && !LaysOutControls(own, panels);
    }

    std::vector<std::wstring> WithQuestion(std::vector<std::wstring> body, const std::vector<std::wstring>& question)
    {
        std::set<std::wstring> shapes;
        for (const auto& text : body)
            shapes.insert(Shape(text));
        for (const auto& text : question)
        {
            if (shapes.insert(Shape(text)).second) body.push_back(text);
        }
        return body;
    }

    std::vector<std::wstring> HistoryLines(const std::vector<std::wstring>& texts)
    {
        std::vector<std::wstring> lines;
        for (const auto& text : texts)
        {
            size_t start = 0;
            while (start <= text.size())
            {
                const size_t end = text.find_first_of(L"\r\n", start);
                const std::wstring line = str::Trim(text.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start));
                if (!line.empty()) lines.push_back(line);
                if (end == std::wstring::npos) break;
                start = end + 1;
            }
        }
        return lines;
    }

    size_t LineCursor::Move(int delta)
    {
        if (m_count == 0) return 0;
        if (delta < 0 && m_at > 0) --m_at;
        if (delta > 0 && m_at + 1 < m_count) ++m_at;
        return m_at;
    }
}
