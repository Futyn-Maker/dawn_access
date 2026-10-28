#include "features/MenuRules.hpp"

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
}
