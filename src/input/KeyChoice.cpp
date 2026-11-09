#include "input/KeyChoice.hpp"

namespace da::input
{
    namespace
    {
        bool StartsWith(const std::wstring& s, const wchar_t* prefix)
        {
            return s.rfind(prefix, 0) == 0;
        }
    }

    std::vector<std::wstring> KeysForDevice(const std::vector<std::wstring>& keys, bool pad, const std::function<bool(const std::wstring&)>& gamepadKey)
    {
        std::vector<std::wstring> own;
        std::vector<std::wstring> other;
        for (const auto& key : keys)
        {
            if (StartsWith(key, L"Gesture")) continue;
            const bool padsOwn = StartsWith(key, L"Gamepad_");
            const bool padKey = padsOwn || (gamepadKey && gamepadKey(key));
            if (pad)
            {
                if (padsOwn)
                    own.push_back(key);
                else if (padKey)
                    other.push_back(key);
            }
            else if (!padKey)
            {
                own.push_back(key);
            }
        }
        own.insert(own.end(), other.begin(), other.end());
        if (own.empty() && !keys.empty()) own.push_back(keys.front());
        return own;
    }
}

namespace da::input
{
    uint16_t ChordGrace::HeldBack(uint16_t buttons, bool holdDown, uint16_t graced, bool kept)
    {
        uint16_t back = 0;
        for (int bit = 0; bit < 16; ++bit)
        {
            const uint16_t b = static_cast<uint16_t>(1u << bit);
            if (!(graced & b)) continue;
            if (!(buttons & b))
            {
                m_given = static_cast<uint16_t>(m_given & ~b);
                m_held[bit] = 0;
                continue;
            }
            // Under the hold the chord has it (HideChord's latch), until it is let go of.
            if (holdDown)
            {
                m_given = static_cast<uint16_t>(m_given | b);
                m_held[bit] = 0;
                continue;
            }
            if (m_given & b) continue;
            if (m_held[bit] < kChordGraceReadings)
            {
                back = static_cast<uint16_t>(back | b);
                if (kept) ++m_held[bit];
                continue;
            }
            m_given = static_cast<uint16_t>(m_given | b);
        }
        return back;
    }
}
