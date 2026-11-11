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

    int ShownKeyIndex(const std::vector<KeyTraits>& keys, IconPriority priority)
    {
        int first = -1;
        int preferred = -1;
        for (size_t i = 0; i < keys.size(); ++i)
        {
            const auto& key = keys[i];
            if (key.name.empty() || key.name == L"None") continue;
            if (first < 0) first = static_cast<int>(i);
            if (preferred >= 0) continue;
            if (priority == IconPriority::Mouse && key.mouseButton) preferred = static_cast<int>(i);
            if (priority == IconPriority::Keyboard && key.digital && !key.mouseButton && !key.gamepad) preferred = static_cast<int>(i);
        }
        return preferred >= 0 ? preferred : first;
    }

    std::wstring CapWords(const std::wstring& keyWords, const std::wstring& chordWords, bool chordMouseButton,
                          const std::function<std::wstring(const std::wstring&, const std::wstring&)>& join)
    {
        if (!chordMouseButton || chordWords.empty() || keyWords.empty() || !join) return keyWords;
        return join(chordWords, keyWords);
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

    std::wstring WayOfKey(const std::vector<KeyModifier>& modifiers)
    {
        double v[3] = {1.0, 0.0, 0.0};
        for (const auto& m : modifiers)
        {
            if (m.kind == KeyModifier::Kind::Negate)
            {
                if (m.x) v[0] = -v[0];
                if (m.y) v[1] = -v[1];
                if (m.z) v[2] = -v[2];
            }
            else if (m.kind == KeyModifier::Kind::Swizzle)
            {
                // The new X, Y, Z take the old axes the order names.
                static constexpr int kOrders[5][3] = {{1, 0, 2}, {2, 1, 0}, {0, 2, 1}, {1, 2, 0}, {2, 0, 1}};
                if (m.order < 0 || m.order > 4) return {};
                const double old[3] = {v[0], v[1], v[2]};
                for (int i = 0; i < 3; ++i)
                    v[i] = old[kOrders[m.order][i]];
            }
            else
            {
                return {};
            }
        }
        if (v[0] > 0.0) return L"Right";
        if (v[0] < 0.0) return L"Left";
        if (v[1] > 0.0) return L"Up";
        if (v[1] < 0.0) return L"Down";
        return {};
    }
}
