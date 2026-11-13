#include "features/GroundRecheck.hpp"

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <string>

namespace da::recheck
{
    Column Grid::ColumnOf(const Vec& at) const
    {
        return {static_cast<int>(std::floor((at.x - origin.x) / cell)), static_cast<int>(std::floor((at.y - origin.y) / cell))};
    }

    std::set<Column> ColumnsAhead(const Grid& grid, const Vec& here, const std::vector<Vec>& ahead, double reach, int radius)
    {
        std::vector<Column> line{grid.ColumnOf(here)};
        Vec from = here;
        double left = reach;
        for (const Vec& corner : ahead)
        {
            if (left <= 0.0) break;
            const double length = std::hypot(corner.x - from.x, corner.y - from.y);
            const double take = std::min(length, left);
            const int samples = std::max(1, static_cast<int>(std::ceil(take / (grid.cell / 2.0))));
            for (int i = 1; i <= samples; ++i)
            {
                const double f = length > 0.0 ? take * i / samples / length : 0.0;
                line.push_back(grid.ColumnOf(Vec{from.x + (corner.x - from.x) * f, from.y + (corner.y - from.y) * f, 0.0}));
            }
            left -= take;
            from = corner;
        }
        return ColumnsAround(line, radius);
    }

    std::set<Column> ColumnsAround(const std::vector<Column>& cells, int radius)
    {
        std::set<Column> out;
        for (const auto& [x, y] : cells)
        {
            for (int dy = -radius; dy <= radius; ++dy)
            {
                for (int dx = -radius; dx <= radius; ++dx)
                {
                    if (dx * dx + dy * dy <= radius * radius + radius) out.insert({x + dx, y + dy});
                }
            }
        }
        return out;
    }

    std::set<Column> ColumnsInBox(const Grid& grid, const Vec& low, const Vec& high, double margin)
    {
        const Column a = grid.ColumnOf(Vec{low.x - margin, low.y - margin, 0.0});
        const Column b = grid.ColumnOf(Vec{high.x + margin, high.y + margin, 0.0});
        std::set<Column> out;
        for (int x = a.first; x <= b.first; ++x)
        {
            for (int y = a.second; y <= b.second; ++y)
                out.insert({x, y});
        }
        return out;
    }

    void Reask::Begin(std::vector<std::pair<StepKey, int>> kept)
    {
        m_steps.clear();
        m_steps.reserve(kept.size());
        for (auto& [key, answer] : kept)
            m_steps.push_back(Step{key, answer, kRefused});
        m_next = 0;
        m_active = !m_steps.empty();
    }

    std::optional<StepKey> Reask::Next()
    {
        if (!m_active || m_next >= m_steps.size())
        {
            m_active = false;
            return std::nullopt;
        }
        return m_steps[m_next].key;
    }

    void Reask::Answer(int answer)
    {
        if (!m_active || m_next >= m_steps.size()) return;
        m_steps[m_next].now = answer;
        if (++m_next >= m_steps.size()) m_active = false;
    }

    std::vector<Column> Reask::Changed() const
    {
        std::vector<Column> out;
        for (const auto& step : m_steps)
        {
            if ((step.now == kRefused) != (step.kept == kRefused)) out.emplace_back(std::get<0>(step.key), std::get<1>(step.key));
        }
        return out;
    }

    size_t Reask::Shut() const
    {
        return static_cast<size_t>(std::count_if(m_steps.begin(), m_steps.end(), [](const Step& s) { return s.kept != kRefused && s.now == kRefused; }));
    }

    size_t Reask::Opened() const
    {
        return static_cast<size_t>(std::count_if(m_steps.begin(), m_steps.end(), [](const Step& s) { return s.kept == kRefused && s.now != kRefused; }));
    }

    bool Weighed(bool doorLeaf, bool stopsHer)
    {
        return doorLeaf || stopsHer;
    }

    bool LeafOfDoor(bool heldItem)
    {
        return !heldItem;
    }

    BoundBy FindBound(bool weakLive, const std::function<bool()>& byPath, const std::function<bool()>& byName)
    {
        if (weakLive) return BoundBy::Weak;
        if (byPath && byPath()) return BoundBy::Path;
        if (byName && byName()) return BoundBy::Name;
        return BoundBy::None;
    }

    const wchar_t* BoundByName(BoundBy by)
    {
        switch (by)
        {
        case BoundBy::Weak: return L"weak pointer";
        case BoundBy::Path: return L"path";
        case BoundBy::Name: return L"name";
        case BoundBy::None: break;
        }
        return L"none";
    }

    bool LivingCreature(std::wstring_view meshPath)
    {
        std::wstring path(meshPath);
        for (auto& c : path)
            c = static_cast<wchar_t>(std::towlower(c));
        static constexpr std::wstring_view kRigs = L"/game/rigs/creatures/";
        const auto at = path.find(kRigs);
        if (at == std::wstring::npos) return false;
        const auto kindAt = at + kRigs.size();
        const auto end = path.find(L'/', kindAt);
        if (end == std::wstring::npos || end == kindAt) return false;
        const std::wstring_view kind = std::wstring_view(path).substr(kindAt, end - kindAt);
        const auto endsWith = [&](std::wstring_view tail) { return kind.size() > tail.size() && kind.substr(kind.size() - tail.size()) == tail; };
        return !endsWith(L"_dead") && !endsWith(L"_cut");
    }

    bool LeafTouches(const Leaf& leaf, const Vec& from, const Vec& to, double radius, double halfHeight)
    {
        if (std::max(from.x, to.x) < leaf.low.x - radius || std::min(from.x, to.x) > leaf.high.x + radius || std::max(from.y, to.y) < leaf.low.y - radius ||
            std::min(from.y, to.y) > leaf.high.y + radius || std::max(from.z, to.z) + halfHeight < leaf.low.z ||
            std::min(from.z, to.z) - halfHeight > leaf.high.z)
            return false;
        return shapes::SweepTouches(leaf.shape, from, to, radius, halfHeight);
    }

    bool StopsPawn(int64_t collisionEnabled, int64_t responseToPawn)
    {
        const bool queried = collisionEnabled == 1 || collisionEnabled == 3 || collisionEnabled == 5;
        return queried && responseToPawn == 2;
    }

    bool Moved(const Box& a, const Box& b, double tolerance)
    {
        const auto off = [&](const Vec& p, const Vec& q)
        { return std::fabs(p.x - q.x) > tolerance || std::fabs(p.y - q.y) > tolerance || std::fabs(p.z - q.z) > tolerance; };
        return off(a.low, b.low) || off(a.high, b.high);
    }

    Box Union(const Box& a, const Box& b)
    {
        return Box{Vec{std::min(a.low.x, b.low.x), std::min(a.low.y, b.low.y), std::min(a.low.z, b.low.z)},
                   Vec{std::max(a.high.x, b.high.x), std::max(a.high.y, b.high.y), std::max(a.high.z, b.high.z)}};
    }

    bool PropFills(const Box& box)
    {
        return box.high.x - box.low.x <= kPropSpanMost && box.high.y - box.low.y <= kPropSpanMost;
    }

    std::set<Column> ColumnsUnder(const Grid& grid, const Box& before, const Box& after, double margin)
    {
        auto columns = ColumnsInBox(grid, before.low, before.high, margin);
        const auto now = ColumnsInBox(grid, after.low, after.high, margin);
        columns.insert(now.begin(), now.end());
        return columns;
    }

    void PropWatch::Arrive()
    {
        m_seen = true; // with no place it stood in before
    }

    bool PropWatch::Update(double now, const std::optional<Box>& box, Box& before, Box& after)
    {
        // A box no prop fills is no reading: everything stands as the last reading left it.
        if (box && !PropFills(*box)) return false;
        if (!m_seen)
        {
            m_seen = true;
            m_rest = box;
            m_last = box;
            return false;
        }
        // Moving since the last reading, or crept away from where it stood at rest.
        const bool stepped = box.has_value() != m_last.has_value() || (box && m_last && Moved(*box, *m_last, kPropTolerance));
        const bool away = box.has_value() != m_rest.has_value() || (box && m_rest && Moved(*box, *m_rest, kPropTolerance));
        m_last = box;
        if (stepped || (away && !m_moving))
        {
            m_moving = true;
            m_stillSince = now;
            return false;
        }
        if (!m_moving || now - m_stillSince < kPropRest) return false;
        m_moving = false;
        // Back where it stood: nothing of the ground has changed.
        const bool same = box.has_value() == m_rest.has_value() && (!box || !Moved(*box, *m_rest, kPropTolerance));
        const auto was = m_rest;
        m_rest = box;
        if (same) return false;
        // A prop with no collision before or after stood nowhere then: the other box stands for both.
        before = was ? *was : *box;
        after = box ? *box : *was;
        return true;
    }
}
