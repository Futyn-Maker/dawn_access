#include "features/CreditsRules.hpp"

#include "core/Strings.hpp"

#include <algorithm>

namespace da::credits
{
    namespace
    {
        // Trimmed, every run of white space (line breaks included) made one space.
        std::wstring Collapsed(const std::wstring& s)
        {
            return str::CollapseWhitespace(s);
        }
    }

    std::wstring LineOf(const std::vector<std::wstring>& entries)
    {
        std::wstring out;
        for (const auto& entry : entries)
        {
            const std::wstring word = Collapsed(entry);
            if (word.empty()) continue;
            if (!out.empty()) out += L", ";
            out += word;
        }
        return out;
    }

    void Roll::Add(const std::wstring& raw)
    {
        const std::wstring line = Collapsed(raw);
        if (line.empty()) return;
        if (!m_lines.empty() && m_lines.back() == line) return;
        m_lines.push_back(line);
    }

    std::vector<std::wstring> Roll::Unread()
    {
        std::vector<std::wstring> out(m_lines.begin() + static_cast<std::ptrdiff_t>(std::min(m_read, m_lines.size())), m_lines.end());
        m_read = m_lines.size();
        return out;
    }

    void Roll::Clear()
    {
        m_lines.clear();
        m_read = 0;
    }

    std::vector<uint64_t> LineIds::Update(const std::vector<LineWidget>& widgets, uint64_t newest)
    {
        // The widget made the roll's newest line since the last poll is set up anew: a new line,
        // though it may never have been seen off the container (taken off and set up again between
        // two polls).
        if (newest != 0 && newest != m_newest) m_known.erase(newest);
        m_newest = newest;
        // A widget off the container is free: its next setting up is a new line.
        std::erase_if(m_known,
                      [&](const auto& entry)
                      {
                          return std::none_of(widgets.begin(), widgets.end(), [&](const LineWidget& w) { return w.widget == entry.first; });
                      });
        std::vector<uint64_t> out;
        out.reserve(widgets.size());
        for (const auto& w : widgets)
        {
            auto known = m_known.find(w.widget);
            // Its scroll gone back: set up again.
            if (known != m_known.end() && w.offset >= 0.0 && known->second.offset >= 0.0 && w.offset < known->second.offset - 1.0)
            {
                m_known.erase(known);
                known = m_known.end();
            }
            if (known == m_known.end()) known = m_known.emplace(w.widget, Known{m_next++, w.offset}).first;
            known->second.offset = w.offset;
            out.push_back(known->second.line);
        }
        return out;
    }

    void LineIds::Clear()
    {
        m_known.clear();
        m_newest = 0;
    }

    std::vector<std::wstring> Taker::Take(const std::vector<Drawn>& lines, double now)
    {
        std::vector<std::wstring> out;
        for (const auto& line : lines)
        {
            if (m_taken.count(line.id)) continue;
            if (line.words.empty())
            {
                const auto waiting = m_waiting.emplace(line.id, now).first;
                // Still waiting for its words: the lines after it wait with it.
                if (now - waiting->second < kWordsWait) break;
                m_waiting.erase(waiting);
                m_taken.insert(line.id);
                continue;
            }
            m_waiting.erase(line.id);
            m_taken.insert(line.id);
            out.push_back(line.words);
        }
        return out;
    }

    bool Taker::Taken(uint64_t id) const
    {
        return m_taken.count(id) > 0;
    }

    void Taker::Clear()
    {
        m_taken.clear();
        m_waiting.clear();
    }

    std::wstring OverlayWords(const std::wstring& room, const std::wstring& time)
    {
        std::wstring place;
        std::wstring part;
        const auto flush = [&]
        {
            const std::wstring piece = Collapsed(part);
            part.clear();
            if (piece.empty()) return;
            if (!place.empty()) place += L", ";
            place += piece;
        };
        for (const wchar_t c : room)
        {
            if (c == L'\r' || c == L'\n')
                flush();
            else
                part += c;
        }
        flush();
        const std::wstring clock = Collapsed(time);
        if (place.empty()) return clock;
        if (clock.empty()) return place;
        return place + L". " + clock;
    }

    std::wstring Overlay::Update(bool shown, const std::wstring& room, const std::wstring& time, double now)
    {
        if (!shown || (room.empty() && time.empty()))
        {
            if (!shown)
            {
                m_shown = false;
                m_room.clear();
                m_clock.Hidden();
            }
            return {};
        }
        const std::wstring clock = Collapsed(time);
        if (!m_shown || room != m_room)
        {
            m_shown = true;
            m_room = room;
            m_clock.Hidden();
            // The clock as it stands now is the first it shows.
            m_clock.Filter({clock}, now);
            return OverlayWords(room, clock);
        }
        const auto said = m_clock.Filter({clock}, now);
        return said.empty() ? std::wstring() : said.front();
    }
}
