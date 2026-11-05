#include "features/Rooms.hpp"

#include <algorithm>
#include <cwctype>
#include <limits>

namespace da::rooms
{
    namespace
    {
        bool StartsNoCase(std::wstring_view s, std::wstring_view prefix)
        {
            if (s.size() < prefix.size()) return false;
            for (size_t i = 0; i < prefix.size(); ++i)
            {
                if (std::towlower(s[i]) != std::towlower(prefix[i])) return false;
            }
            return true;
        }

        bool Digit(wchar_t c)
        {
            return c >= L'0' && c <= L'9';
        }

        bool Upper(wchar_t c)
        {
            return std::iswupper(c) != 0;
        }

        // The words of a name: split at underscores, where a capital follows a small letter or a
        // digit, where a capital begins a word after a run of capitals, and between letters and
        // digits.
        std::vector<std::wstring> Words(std::wstring_view s)
        {
            std::vector<std::wstring> words;
            std::wstring word;
            const auto flush = [&]
            {
                if (!word.empty()) words.push_back(word);
                word.clear();
            };
            for (size_t i = 0; i < s.size(); ++i)
            {
                const wchar_t c = s[i];
                if (c == L'_' || c == L' ' || c == L'-')
                {
                    flush();
                    continue;
                }
                if (!word.empty())
                {
                    const wchar_t last = s[i - 1]; // as written, before the word took it in lower case
                    const bool next = i + 1 < s.size();
                    if ((Upper(c) && (std::iswlower(last) || Digit(last))) || (Digit(c) != Digit(last)) ||
                        (Upper(c) && Upper(last) && next && std::iswlower(s[i + 1])))
                        flush();
                }
                word.push_back(static_cast<wchar_t>(std::towlower(c)));
            }
            flush();
            return words;
        }
    }

    std::wstring Key(std::wstring_view name, bool* broad)
    {
        if (broad) *broad = false;
        std::wstring_view rest = name;
        if (StartsNoCase(rest, L"T_")) rest.remove_prefix(2);
        bool wide = false;
        if (StartsNoCase(rest, L"GlobalReverb_") || StartsNoCase(rest, L"ReverbGlobal_"))
        {
            wide = true;
            rest.remove_prefix(13);
        }
        else if (StartsNoCase(rest, L"Reverb_Global_"))
        {
            wide = true;
            rest.remove_prefix(14);
        }
        else if (StartsNoCase(rest, L"Reverb_"))
        {
            rest.remove_prefix(7);
        }
        else
        {
            return {};
        }
        auto words = Words(rest);
        while (words.size() > 1 && (std::all_of(words.back().begin(), words.back().end(), [](wchar_t c) { return Digit(c); }) || words.back().size() == 1))
            words.pop_back();
        if (words.empty()) return {};
        std::wstring key;
        for (const auto& w : words)
        {
            if (!key.empty()) key += L'_';
            key += w;
        }
        if (broad) *broad = wide;
        return key;
    }

    Zone MakeZone(std::wstring name, shapes::Shape shape)
    {
        Zone z;
        z.key = Key(name, &z.broad);
        z.name = std::move(name);
        z.shape = std::move(shape);
        if (!z.shape.Empty())
        {
            z.shape.Bounds(z.low, z.high);
            z.size = (z.high.x - z.low.x) * (z.high.y - z.low.y) * (z.high.z - z.low.z);
        }
        return z;
    }

    double Outside(const Zone& zone, const Vec& point)
    {
        if (zone.shape.Empty()) return std::numeric_limits<double>::infinity();
        return shapes::Distance(zone.shape, point, point);
    }

    int ZoneAt(const std::vector<Zone>& zones, const Vec& point)
    {
        int best = -1;
        for (size_t i = 0; i < zones.size(); ++i)
        {
            const Zone& z = zones[i];
            if (z.shape.Empty()) continue;
            if (point.x < z.low.x || point.x > z.high.x || point.y < z.low.y || point.y > z.high.y || point.z < z.low.z || point.z > z.high.z) continue;
            if (best >= 0 && z.size >= zones[static_cast<size_t>(best)].size) continue;
            if (Outside(z, point) > 1e-6) continue;
            best = static_cast<int>(i);
        }
        return best;
    }

    void Crossing::Reset()
    {
        m_started = false;
        m_room.clear();
        m_spoken.clear();
    }

    std::wstring Crossing::Update(const std::vector<Zone>& zones, const Vec& middle)
    {
        if (zones.empty()) return {};
        const int candidate = ZoneAt(zones, middle);
        int current = -1;
        for (size_t i = 0; i < zones.size() && !m_room.empty(); ++i)
        {
            if (zones[i].name == m_room) current = static_cast<int>(i);
        }
        int next = current;
        if (candidate != current)
        {
            const bool toBroad = candidate >= 0 && zones[static_cast<size_t>(candidate)].broad;
            const bool fromRoom = current >= 0 && !zones[static_cast<size_t>(current)].broad;
            if (current < 0)
                next = candidate;
            else if (candidate >= 0 && !toBroad && zones[static_cast<size_t>(candidate)].size < zones[static_cast<size_t>(current)].size)
                next = candidate; // a narrower room she has walked into
            else if (Outside(zones[static_cast<size_t>(current)], middle) > (toBroad && fromRoom ? kBroadMargin : kCrossMargin))
                next = candidate; // she has left the room
        }
        const bool first = !m_started;
        m_started = true;
        const std::wstring before = m_spoken;
        m_room = next >= 0 ? zones[static_cast<size_t>(next)].name : std::wstring();
        m_spoken = next >= 0 ? zones[static_cast<size_t>(next)].spoken : std::wstring();
        if (first || m_spoken.empty() || m_spoken == before) return {};
        return m_spoken;
    }

    void Area::Reset(size_t zones)
    {
        m_cells.assign(zones, 0);
    }

    void Area::Add(int zone)
    {
        if (zone >= 0 && static_cast<size_t>(zone) < m_cells.size()) ++m_cells[static_cast<size_t>(zone)];
    }

    bool Area::Covers(int zone) const
    {
        return Cells(zone) >= kAreaCells;
    }

    size_t Area::Cells(int zone) const
    {
        return zone >= 0 && static_cast<size_t>(zone) < m_cells.size() ? m_cells[static_cast<size_t>(zone)] : 0;
    }

    Verdict Judge(const Facts& f, Verdict before)
    {
        if (f.offered || f.reach == 1) return Verdict::In;
        const bool judged = before == Verdict::In || before == Verdict::Apart;
        if (!f.settled || (f.reach == 0 && f.running)) return judged ? before : Verdict::Waits;
        return f.proves && (f.reach == 2 || f.reach == 3) ? Verdict::Apart : Verdict::In;
    }

    bool WayOffered(bool enclosed, int reach, bool pathReach)
    {
        return pathReach || (!enclosed && reach != 3);
    }
}
