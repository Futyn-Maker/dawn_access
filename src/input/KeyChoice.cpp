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
