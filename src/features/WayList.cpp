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
            if (way.kept)
            {
                onRoadOf[i] = way.onRoadOf;
                if (way.way && way.onRoadOf < 0 && !way.behind && !way.unreachable && !way.unjudged) listedWays.insert(i);
                continue;
            }
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

    bool CapsuleOverlaps(const Box& box, const Vec& feet, double radius)
    {
        const double radians = box.yaw * std::numbers::pi / 180.0;
        const double c = std::cos(radians);
        const double s = std::sin(radians);
        const double dx = feet.x - box.middle.x;
        const double dy = feet.y - box.middle.y;
        const double lx = dx * c + dy * s;
        const double ly = -dx * s + dy * c;
        return std::hypot(std::max(std::fabs(lx) - box.size.x / 2.0, 0.0), std::max(std::fabs(ly) - box.size.y / 2.0, 0.0)) <= radius;
    }

    bool RoadEntersBox(const Box& box, const std::vector<Vec>& road, double radius, double halfHeight, double step)
    {
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
                if (CapsuleOverlaps(box, p, radius)) return true;
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
        for (auto& e : entries)
            e.kept = m_frozen && e.passed && e.epoch == m_epoch;
        const auto onRoadOf = RowRule(entries, enters);
        for (size_t i = 0; i < entries.size(); ++i)
        {
            entries[i].onRoadOf = onRoadOf[i];
            entries[i].passed = true;
            entries[i].epoch = m_epoch;
        }
    }

    bool IsNews(std::optional<double> turnedOnAt, double now)
    {
        return turnedOnAt && now - *turnedOnAt <= kNewsWithin;
    }

    bool ResumesList(bool paused, bool onMappedGround)
    {
        return paused || onMappedGround;
    }

    bool Reopened(int reachWhenFrozen, int reachNow)
    {
        return (reachWhenFrozen == 2 || reachWhenFrozen == 3) && reachNow == 1;
    }

    double PathStray(double ahead, double coneDegrees)
    {
        const double cone = std::clamp(std::fabs(coneDegrees), 0.0, 60.0) * std::numbers::pi / 180.0;
        return std::clamp(std::max(0.0, ahead) * std::tan(cone), 0.0, kPathStrayMax);
    }

    bool LeftForThing(const Vec& thing, const Vec& way, bool thingGoneTo)
    {
        if (!thingGoneTo) return false;
        return std::hypot(thing.x - way.x, thing.y - way.y) < kTwinAcross && std::fabs(thing.z - way.z) < kTwinUp;
    }

    double VolumeGoalRadius(const Box& box, double capsuleRadius, double cell)
    {
        const double half = std::min(box.high.x - box.low.x, box.high.y - box.low.y) / 2.0;
        return std::max(1.5, (half + capsuleRadius) / cell);
    }

    bool OnlyTried(const std::vector<std::wstring>& animations)
    {
        return !animations.empty() &&
               std::all_of(animations.begin(), animations.end(), [](const std::wstring& a) { return a.rfind(L"ABP_Door_Locked_", 0) == 0; });
    }

    bool UsableBy(const UseRule& rule)
    {
        if (rule.requiresAnimation && !rule.animated) return false;
        return rule.anyone || rule.hers;
    }

    bool InRange(double distance, double range, bool listedBefore)
    {
        return distance <= range + (listedBefore ? kRangeMargin : 0.0);
    }

    std::vector<size_t> StoodIn(const std::vector<bool>& inside)
    {
        std::vector<size_t> out;
        for (size_t i = 0; i < inside.size(); ++i)
        {
            if (inside[i]) out.push_back(i);
        }
        return out;
    }

    int StretchOn(const std::vector<Vec>& corners, size_t count, const Vec& here, double reach, double rise)
    {
        count = std::min(count, corners.size());
        if (count == 0) return -1;
        int stretch = -1;
        double best = reach;
        const auto consider = [&](const Vec& point, int index)
        {
            if (std::fabs(here.z - point.z) > rise) return;
            const double over = std::hypot(here.x - point.x, here.y - point.y);
            if (over < best)
            {
                best = over;
                stretch = index;
            }
        };
        consider(corners[0], 0);
        for (size_t i = 1; i < count; ++i)
        {
            const Vec& a = corners[i - 1];
            const Vec& b = corners[i];
            const double abx = b.x - a.x;
            const double aby = b.y - a.y;
            const double length = abx * abx + aby * aby;
            double along = 0.0;
            if (length > 1.0) along = std::clamp(((here.x - a.x) * abx + (here.y - a.y) * aby) / length, 0.0, 1.0);
            consider(Vec{a.x + abx * along, a.y + aby * along, a.z + (b.z - a.z) * along}, static_cast<int>(i));
        }
        return stretch;
    }

    Vec SlideWalk(const Sweep& sweep, const Vec& from, const Vec& to, int& touches)
    {
        Vec pos = from;
        touches = 0;
        for (int i = 0; i <= kPassageTouches; ++i)
        {
            const double length = std::sqrt((to.x - pos.x) * (to.x - pos.x) + (to.y - pos.y) * (to.y - pos.y) + (to.z - pos.z) * (to.z - pos.z));
            if (length <= 1.0) return to;
            const SweepHit hit = sweep(pos, to);
            if (!hit.hit) return to;
            if (i == kPassageTouches) break;
            ++touches;
            double nx = hit.normal.x;
            double ny = hit.normal.y;
            const double n = std::hypot(nx, ny);
            if (n < 1e-3) break; // a floor or a ceiling across the way
            nx /= n;
            ny /= n;
            if (hit.penetrating)
            {
                // Begun pressed in: out along the contact, as the movement pulls her out.
                pos = Vec{pos.x + nx * (hit.depth + 1.0), pos.y + ny * (hit.depth + 1.0), pos.z};
                continue;
            }
            // Up to the contact, a hair short of it and a hair off the surface.
            const double go = std::clamp(hit.distance - 0.5, 0.0, length);
            const double f = go / length;
            pos = Vec{pos.x + (to.x - pos.x) * f + nx * 0.2, pos.y + (to.y - pos.y) * f + ny * 0.2, pos.z + (to.z - pos.z) * f};
            // The rest of the move along the surface: its part into the surface taken away.
            double rx = to.x - pos.x;
            double ry = to.y - pos.y;
            const double into = rx * nx + ry * ny;
            if (into < 0.0)
            {
                rx -= nx * into;
                ry -= ny * into;
            }
            const double rest = std::hypot(rx, ry);
            if (rest < 1.0) break; // head on
            const Vec along{pos.x + rx, pos.y + ry, pos.z};
            const SweepHit slid = sweep(pos, along);
            if (!slid.hit)
            {
                pos = along;
                continue;
            }
            if (slid.penetrating) break;
            const double g = std::clamp(slid.distance - 0.5, 0.0, rest) / rest;
            pos = Vec{pos.x + rx * g, pos.y + ry * g, pos.z};
        }
        return pos;
    }

    PassageWay PassageWalk(const Sweep& sweep, const Vec& a, const Vec& b)
    {
        PassageWay way;
        const double dx = b.x - a.x;
        const double dy = b.y - a.y;
        const double length = std::hypot(dx, dy);
        if (length < 1.0) return way;
        // The engine's frame: x ahead, y to the right.
        const double rightX = -dy / length;
        const double rightY = dx / length;
        const int steps = static_cast<int>(std::floor(kPassageShiftMax / kPassageShift + 1e-9));
        for (int k = 0; k <= 2 * steps; ++k)
        {
            const double shift = (k == 0 ? 0.0 : ((k + 1) / 2) * kPassageShift * (k % 2 == 1 ? 1.0 : -1.0));
            const Vec from{a.x + rightX * shift, a.y + rightY * shift, a.z};
            const Vec to{b.x + rightX * shift, b.y + rightY * shift, b.z};
            int touches = 0;
            const Vec reached = SlideWalk(sweep, from, to, touches);
            if (k == 0) way.reached = reached;
            if (std::hypot(reached.x - to.x, reached.y - to.y) <= kPassageArrive && std::fabs(reached.z - to.z) <= kPassageArrive)
            {
                way.passed = true;
                way.shift = shift;
                way.touches = touches;
                return way;
            }
        }
        return way;
    }

    void PassageLinks(const std::vector<Passage>& passages, const Vec& middle, double cellSize,
                      const std::function<bool(const Vec& end, grid::Cell& cell, Vec& cellMiddle)>& cellAt, std::vector<grid::Link>& out)
    {
        for (const Passage& passage : passages)
        {
            if (!passage.free) continue;
            for (int side = 0; side < 2; ++side)
            {
                const Vec& here = side == 0 ? passage.a : passage.b;
                const Vec& there = side == 0 ? passage.b : passage.a;
                if (std::hypot(middle.x - here.x, middle.y - here.y) > kPassageReach || std::fabs(middle.z - here.z) > kPassageRise) continue;
                grid::Cell cell;
                Vec cellMiddle;
                if (!cellAt(there, cell, cellMiddle)) continue;
                out.push_back(grid::Link{cell, std::hypot(cellMiddle.x - middle.x, cellMiddle.y - middle.y) / cellSize});
            }
        }
    }
}
