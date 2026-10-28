#include "features/WayList.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <numbers>
#include <set>

namespace da::ways
{
    namespace
    {
        // The reached level at a column nearest `level` and within one of it; false when the map
        // reached none there.
        bool LevelNear(const grid::Flood& flood, int x, int y, int level, int& out)
        {
            const auto* levels = flood.LevelsAt(x, y);
            if (!levels) return false;
            bool found = false;
            for (const int l : *levels)
            {
                if (std::abs(l - level) > 1) continue;
                if (!found || std::abs(l - level) < std::abs(out - level))
                {
                    out = l;
                    found = true;
                }
            }
            return found;
        }

        // The straight line between two reached cells runs over reached ground all along: a column
        // for each cell of the longer side, the one nearest the line, each reached on a level
        // within one of the last, ending on the far cell's own level.
        bool ClearOverMap(const grid::Flood& flood, grid::Cell a, grid::Cell b)
        {
            const int steps = std::max(std::abs(a.x - b.x), std::abs(a.y - b.y));
            if (steps == 0) return std::abs(a.level - b.level) <= 1;
            int level = a.level;
            for (int i = 1; i <= steps; ++i)
            {
                const int x = a.x + static_cast<int>(std::lround(static_cast<double>(b.x - a.x) * i / steps));
                const int y = a.y + static_cast<int>(std::lround(static_cast<double>(b.y - a.y) * i / steps));
                if (i == steps) return x == b.x && y == b.y && std::abs(b.level - level) <= 1;
                if (!LevelNear(flood, x, y, level, level)) return false;
            }
            return true;
        }
    }

    std::vector<grid::Cell> StraightOverMap(const grid::Flood& flood, const std::vector<grid::Cell>& path, size_t maxRun)
    {
        return grid::Corners(path, [&](grid::Cell a, grid::Cell b) { return ClearOverMap(flood, a, b); }, maxRun);
    }

    std::vector<int> RowRule(const std::vector<Entry>& entries, const Enters& enters)
    {
        std::vector<size_t> order(entries.size());
        for (size_t i = 0; i < order.size(); ++i)
            order[i] = i;
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return entries[a].distance > entries[b].distance; });
        std::vector<int> onRoadOf(entries.size(), -1);
        std::set<size_t> listedWays;
        for (const size_t i : order)
        {
            const Entry& way = entries[i];
            const bool judged = way.way && way.hasBox && !way.behind && !way.unreachable && !way.unjudged && !way.reached && !way.chosen;
            if (judged)
            {
                for (const size_t j : order)
                {
                    if (j == i) continue;
                    const Entry& other = entries[j];
                    const bool counts = !other.closedOff &&
                                        (other.chosen || (!other.behind && !other.unreachable && !other.unjudged && (!other.way || listedWays.count(j) > 0)));
                    if (!counts || !enters(i, j)) continue;
                    onRoadOf[i] = static_cast<int>(j);
                    break;
                }
            }
            if (way.way && onRoadOf[i] < 0 && !way.behind && !way.unreachable && !way.unjudged) listedWays.insert(i);
        }
        return onRoadOf;
    }

    Box TurnedBox(const Vec& middle, const Vec& size, double yaw)
    {
        Box box;
        box.middle = middle;
        box.size = size;
        box.yaw = yaw;
        const double radians = yaw * std::numbers::pi / 180.0;
        const double c = std::fabs(std::cos(radians));
        const double s = std::fabs(std::sin(radians));
        const double hx = c * size.x / 2.0 + s * size.y / 2.0;
        const double hy = s * size.x / 2.0 + c * size.y / 2.0;
        const double hz = size.z / 2.0;
        box.low = Vec{middle.x - hx, middle.y - hy, middle.z - hz};
        box.high = Vec{middle.x + hx, middle.y + hy, middle.z + hz};
        return box;
    }

    bool RoadEntersBox(const Box& box, const std::vector<Vec>& road, double radius, double halfHeight, double step)
    {
        const double radians = box.yaw * std::numbers::pi / 180.0;
        const double c = std::cos(radians);
        const double s = std::sin(radians);
        for (size_t i = 1; i < road.size(); ++i)
        {
            const Vec& a = road[i - 1];
            const Vec& b = road[i];
            const int steps = std::max(1, static_cast<int>(std::hypot(b.x - a.x, b.y - a.y) / step));
            for (int k = 0; k <= steps; ++k)
            {
                const double f = static_cast<double>(k) / steps;
                Vec p{a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f, a.z + (b.z - a.z) * f};
                if (p.x < box.low.x - radius || p.x > box.high.x + radius || p.y < box.low.y - radius || p.y > box.high.y + radius) continue;
                p.z = std::clamp((box.low.z + box.high.z) / 2.0, p.z, p.z + 2.0 * halfHeight);
                if (p.z < box.low.z || p.z > box.high.z) continue;
                const double dx = p.x - box.middle.x;
                const double dy = p.y - box.middle.y;
                const double lx = dx * c + dy * s;
                const double ly = -dx * s + dy * c;
                const double distance = std::hypot(std::max(std::fabs(lx) - box.size.x / 2.0, 0.0), std::max(std::fabs(ly) - box.size.y / 2.0, 0.0));
                if (distance <= radius) return true;
            }
        }
        return false;
    }

    void Settling::Reset()
    {
        m_settled = false;
        m_keep = false;
        m_once = false;
        m_begun = false;
        m_kept = false;
        m_startedAt = 0.0;
    }

    void Settling::Begin(double now)
    {
        m_keep = m_once;
        m_kept = m_once;
        m_settled = false;
        m_begun = true;
        m_startedAt = now;
    }

    void Settling::Clear()
    {
        m_settled = true;
        m_keep = false;
        m_kept = false;
        m_once = true;
    }

    bool Settling::Update(double now, bool done, size_t judged, size_t targets)
    {
        if (m_settled || !m_begun) return false;
        if (!done && judged < targets && now - m_startedAt < kSettleCap) return false;
        m_settled = true;
        m_keep = false;
        m_once = true;
        return true;
    }

    bool Lister::Due(const Settling& settle, bool settlesNow, double now) const
    {
        return settle.Settled() && (settlesNow || now - m_judgedAt >= 1.0);
    }

    void Lister::Judge(std::vector<Entry>& entries, const Settling& settle, bool due, bool roads, double now, const Enters& enters)
    {
        if (!roads && (settle.Settled() || settle.Keeping())) return;
        for (auto& e : entries)
        {
            if (settle.Keeping() && e.passed) continue;
            e.unjudged = e.way && (!settle.Settled() || !(e.known || (settle.Kept() && e.earlier)));
        }
        if (!due) return;
        m_judgedAt = now;
        const auto onRoadOf = RowRule(entries, enters);
        for (size_t i = 0; i < entries.size(); ++i)
        {
            entries[i].onRoadOf = onRoadOf[i];
            entries[i].passed = true;
        }
    }
}
