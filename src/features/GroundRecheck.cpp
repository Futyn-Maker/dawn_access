#include "features/GroundRecheck.hpp"

#include <algorithm>
#include <cmath>

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

    bool PropWatch::Update(double now, const std::optional<Box>& box, Box& before, Box& after)
    {
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
