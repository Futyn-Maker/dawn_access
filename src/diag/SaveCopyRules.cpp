#include "diag/SaveCopyRules.hpp"

#include <algorithm>
#include <cwctype>

namespace da::savecopy
{
    namespace
    {
        std::wstring Lower(std::wstring s)
        {
            for (auto& c : s)
                c = static_cast<wchar_t>(std::towlower(c));
            return s;
        }

        bool StartsWithNoCase(const std::wstring& s, const std::wstring& prefix)
        {
            return s.size() >= prefix.size() && Lower(s.substr(0, prefix.size())) == Lower(prefix);
        }

        bool EndsWithNoCase(const std::wstring& s, const std::wstring& suffix)
        {
            return s.size() >= suffix.size() && Lower(s.substr(s.size() - suffix.size())) == Lower(suffix);
        }
    }

    bool Watch::Due(const Stamp& now)
    {
        const bool steady = now.Known() && now == m_seen;
        m_seen = now;
        return steady && now != m_copied;
    }

    std::wstring CopyName(const std::wstring& stamp, const std::wstring& label)
    {
        std::wstring clean;
        for (const wchar_t c : label)
        {
            if (clean.size() >= 48) break;
            if (std::iswalnum(c) || c == L'_' || c == L'-') clean.push_back(c);
        }
        return clean.empty() ? stamp + L".sav" : stamp + L"-" + clean + L".sav";
    }

    std::wstring MapLabel(const std::wstring& mapPath)
    {
        // "/Game/Levels/game/<act>/..." names its act's folder.
        const std::wstring lower = Lower(mapPath);
        const std::wstring folder = L"/levels/game/";
        if (const auto at = lower.find(folder); at != std::wstring::npos)
        {
            const size_t start = at + folder.size();
            const size_t end = mapPath.find(L'/', start);
            const std::wstring act = mapPath.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
            if (!act.empty()) return act;
        }
        // Else the map's own name: "/Game/Maps/LV_Act_2_2_Persistent.LV_Act_2_2_Persistent" as Act_2_2.
        std::wstring name = mapPath.substr(mapPath.find_last_of(L'/') == std::wstring::npos ? 0 : mapPath.find_last_of(L'/') + 1);
        if (const auto dot = name.find(L'.'); dot != std::wstring::npos) name = name.substr(0, dot);
        if (StartsWithNoCase(name, L"LV_")) name = name.substr(3);
        if (EndsWithNoCase(name, L"_Persistent")) name = name.substr(0, name.size() - 11);
        return name;
    }

    std::vector<std::wstring> Pruned(std::vector<std::wstring> names, size_t keep)
    {
        std::sort(names.begin(), names.end());
        if (names.size() <= keep) return {};
        names.resize(names.size() - keep);
        return names;
    }
}
