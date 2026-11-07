#include "features/WorldStream.hpp"

#include <algorithm>

namespace da::stream
{
    bool Kept(const Basis& basis, bool wall, bool cameIn, bool present, bool streamed)
    {
        if (!basis.hit) return !cameIn;
        if (present) return wall || !cameIn;
        return streamed;
    }

    bool Askable(const Basis& basis, bool present, bool streamed)
    {
        return !basis.hit || present || !streamed;
    }

    bool Reanchors(bool anchored, bool sameBody, double fromOrigin)
    {
        return !anchored || !sameBody || fromOrigin >= kGridReach;
    }

    bool Settled(bool adding, bool removing, bool allShown, double sinceChange)
    {
        return !adding && !removing && allShown && sinceChange >= kSettle;
    }

    int World::Id(const std::wstring& name)
    {
        const auto known = std::find(m_names.begin(), m_names.end(), name);
        if (known != m_names.end()) return static_cast<int>(known - m_names.begin());
        m_names.push_back(name);
        m_present.push_back(false);
        m_streamed.push_back(false);
        return static_cast<int>(m_names.size() - 1);
    }

    const std::wstring& World::Name(int id) const
    {
        static const std::wstring none;
        return id >= 0 && static_cast<size_t>(id) < m_names.size() ? m_names[static_cast<size_t>(id)] : none;
    }

    World::Change World::Count(const std::vector<std::wstring>& levels, const std::set<std::wstring>& sections, bool fresh)
    {
        std::vector<bool> now(m_names.size(), false);
        for (const auto& name : levels)
        {
            const int id = Id(name);
            if (now.size() < m_names.size()) now.resize(m_names.size(), false);
            now[static_cast<size_t>(id)] = true;
        }
        m_present.resize(m_names.size(), false);
        Change change;
        change.fresh = fresh || !m_counted;
        m_counted = true;
        for (size_t i = 0; i < m_names.size(); ++i)
        {
            const bool before = m_present[i] && !change.fresh;
            if (now[i] && !before) change.in.push_back(static_cast<int>(i));
            if (!now[i] && before) change.out.push_back(static_cast<int>(i));
        }
        if (!change.fresh)
        {
            // The parts of a section come and go with it.
            const auto named = [&](int id) { return sections.count(m_names[static_cast<size_t>(id)]) > 0; };
            const bool streaming = std::any_of(change.in.begin(), change.in.end(), named) || std::any_of(change.out.begin(), change.out.end(), named);
            if (streaming)
            {
                for (int id : change.in)
                    m_streamed[static_cast<size_t>(id)] = true;
                for (int id : change.out)
                    m_streamed[static_cast<size_t>(id)] = true;
            }
        }
        for (size_t i = 0; i < m_names.size(); ++i)
        {
            if (sections.count(m_names[i]) > 0) m_streamed[i] = true;
        }
        m_present = now;
        return change;
    }

    bool World::Present(int id) const
    {
        if (id < 0 || static_cast<size_t>(id) >= m_present.size()) return true;
        return m_present[static_cast<size_t>(id)];
    }

    bool World::Streamed(int id) const
    {
        return id >= 0 && static_cast<size_t>(id) < m_streamed.size() && m_streamed[static_cast<size_t>(id)];
    }
}
