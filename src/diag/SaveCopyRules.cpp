#include "diag/SaveCopyRules.hpp"

#include <algorithm>
#include <cstring>
#include <cwctype>
#include <set>
#include <string_view>

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

    std::wstring SaveLabel(const std::vector<char>& bytes)
    {
        // A tagged property: its name and type as FStrings (int32 length with the NUL, then the
        // characters), int32 size, int32 array index, a has-GUID byte (16 bytes of GUID after it when
        // set), then the value, here an FString.
        static const std::string kName = "LevelStreamingSectionName";
        std::string marker(4, '\0');
        const int32_t nameLength = static_cast<int32_t>(kName.size() + 1);
        std::memcpy(marker.data(), &nameLength, 4);
        marker += kName;
        marker.push_back('\0');
        const std::string_view all(bytes.data(), bytes.size());
        const size_t at = all.find(marker);
        if (at == std::string_view::npos) return {};
        size_t pos = at + marker.size();
        auto readInt = [&](int32_t& value)
        {
            if (pos + 4 > all.size()) return false;
            std::memcpy(&value, all.data() + pos, 4);
            pos += 4;
            return true;
        };
        auto readAscii = [&](std::string& out)
        {
            int32_t length = 0;
            if (!readInt(length) || length <= 0 || length > 256 || pos + static_cast<size_t>(length) > all.size()) return false;
            out.assign(all.data() + pos, static_cast<size_t>(length) - 1);
            pos += static_cast<size_t>(length);
            return true;
        };
        std::string type;
        int32_t size = 0, index = 0;
        if (!readAscii(type) || type != "StrProperty" || !readInt(size) || !readInt(index) || pos >= all.size()) return {};
        if (all[pos++] != 0) pos += 16;
        std::string value;
        if (!readAscii(value)) return {};
        std::wstring label(value.begin(), value.end());
        if (StartsWithNoCase(label, L"lv_")) label = label.substr(3);
        return label;
    }

    std::vector<std::wstring> Pruned(std::vector<std::wstring> names, size_t keep)
    {
        std::sort(names.begin(), names.end());
        if (names.size() <= keep) return {};
        // "<yyyymmdd-hhmmss>" is fifteen characters; what follows, before ".sav", is the label.
        auto labelOf = [](const std::wstring& name)
        {
            std::wstring rest = name.size() > 15 ? name.substr(15) : std::wstring();
            if (EndsWithNoCase(rest, L".sav")) rest = rest.substr(0, rest.size() - 4);
            if (!rest.empty() && rest.front() == L'-') rest = rest.substr(1);
            return Lower(rest);
        };
        std::set<std::wstring> firstSeen;
        std::vector<std::wstring> pruned;
        for (size_t i = 0; i + keep < names.size(); ++i)
        {
            const auto label = labelOf(names[i]);
            if (!label.empty() && firstSeen.insert(label).second) continue;
            pruned.push_back(names[i]);
        }
        return pruned;
    }
}
