#include "features/ExploreText.hpp"

#include "core/Strings.hpp"

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <cwctype>

namespace da::explore
{
    std::wstring NameWords(std::wstring_view objectName)
    {
        std::wstring name(objectName);
        // The game's designers prefix a thing's name with its kind: an interaction, an
        // activity, a trigger, a marker, a prop, a Blueprint.
        for (const wchar_t* prefix : {L"BP_Bates_", L"BP_", L"Bates_", L"I_", L"A_", L"T_", L"M_", L"P_", L"UL_", L"PAO_", L"MSQ_", L"LLS_"})
        {
            if (str::StartsWith(name, prefix))
            {
                name.erase(0, wcslen(prefix));
                break;
            }
        }
        // A cooked Blueprint class ends in "_C".
        if (str::EndsWith(name, L"_C")) name.erase(name.size() - 2);
        while (!name.empty() && (iswdigit(name.back()) || name.back() == L'_'))
            name.pop_back();
        std::wstring out;
        for (size_t i = 0; i < name.size(); ++i)
        {
            const wchar_t c = name[i];
            if (c == L'_')
            {
                out += L' ';
                continue;
            }
            if (i > 0 && iswupper(c))
            {
                const wchar_t previous = name[i - 1];
                const bool opensWord = !iswupper(previous) && previous != L'_';
                const bool closesRun = iswupper(previous) && i + 1 < name.size() && iswlower(name[i + 1]);
                if (opensWord || closesRun) out += L' ';
            }
            out += c;
        }
        return str::CollapseWhitespace(out);
    }

    double NormalizeDegrees(double degrees)
    {
        degrees = std::fmod(degrees + 180.0, 360.0);
        if (degrees < 0) degrees += 360.0;
        return degrees - 180.0;
    }

    const wchar_t* SectorKey(double bearing)
    {
        const int sector = (static_cast<int>(std::lround(bearing / 45.0)) % 8 + 8) % 8;
        static const wchar_t* const keys[] = {L"dir8.ahead",  L"dir8.aheadright", L"dir8.right", L"dir8.behindright",
                                              L"dir8.behind", L"dir8.behindleft", L"dir8.left",  L"dir8.aheadleft"};
        return keys[sector];
    }

    std::wstring Metres(double centimetres)
    {
        return std::to_wstring(std::max<long>(1, std::lround(centimetres / 100.0)));
    }
}
