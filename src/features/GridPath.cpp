#include "features/GridPath.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <utility>

namespace da::grid
{
    namespace
    {
        constexpr double kDiagonal = 1.4142135623730951;
    }

    std::vector<Cell> TreeWay(const std::vector<Cell>& from, const std::vector<Cell>& to, size_t* sharedOut)
    {
        std::vector<Cell> way;
        size_t shared = 0;
        while (shared < from.size() && shared < to.size() && from[shared] == to[shared])
            ++shared;
        if (sharedOut) *sharedOut = shared;
        if (shared == 0) return way;
        for (size_t i = from.size(); i-- > shared - 1;)
            way.push_back(from[i]);
        for (size_t i = shared; i < to.size(); ++i)
            way.push_back(to[i]);
        return way;
    }

    double Distance(Cell a, Cell b)
    {
        const double dx = static_cast<double>(a.x - b.x);
        const double dy = static_cast<double>(a.y - b.y);
        return std::sqrt(dx * dx + dy * dy);
    }

    Search::Search(Cell start, Cell goal, double goalRadius, int goalLevels, int maxRadius, int maxExpansions, double levelWeight,
                   std::function<bool(Cell)> fits)
        : m_start(start), m_goal(goal), m_goalRadius(goalRadius), m_goalLevels(goalLevels), m_maxRadius(maxRadius), m_maxExpansions(maxExpansions),
          m_levelWeight(levelWeight), m_fits(std::move(fits))
    {
        m_best[start] = 0.0;
        m_nearest = start;
        m_nearestDistance = Nearness(start);
        m_open.push(Open{Heuristic(start), 0.0, start});
        if (AtGoal(start))
        {
            m_state = State::Found;
            m_reached = start;
        }
    }

    // The octile distance: the cost of the cheapest run of straight and diagonal steps.
    double Search::Heuristic(Cell a) const
    {
        const double dx = std::fabs(static_cast<double>(a.x - m_goal.x));
        const double dy = std::fabs(static_cast<double>(a.y - m_goal.y));
        return std::max(dx, dy) + (kDiagonal - 1.0) * std::min(dx, dy);
    }

    // How near the goal a cell is for keeping the nearest one: over the ground, and the
    // levels between them by their weight.
    double Search::Nearness(Cell a) const
    {
        return Distance(a, m_goal) + m_levelWeight * std::abs(a.level - m_goal.level);
    }

    bool Search::AtGoal(Cell a) const
    {
        if (Distance(a, m_goal) > m_goalRadius) return false;
        if (m_goalLevels >= 0 && std::abs(a.level - m_goal.level) > m_goalLevels) return false;
        return !m_fits || m_fits(a);
    }

    Search::State Search::Step(int steps, const Passable& passable)
    {
        while (m_state == State::Running && steps-- > 0)
        {
            if (m_open.empty() || m_expanded >= m_maxExpansions)
            {
                m_state = State::Failed;
                break;
            }
            const Open current = m_open.top();
            m_open.pop();
            if (m_closed.count(current.cell)) continue;
            m_closed.insert(current.cell);
            ++m_expanded;
            if (const double d = Nearness(current.cell); d < m_nearestDistance)
            {
                m_nearestDistance = d;
                m_nearest = current.cell;
            }
            if (AtGoal(current.cell))
            {
                m_state = State::Found;
                m_reached = current.cell;
                break;
            }
            for (int dy = -1; dy <= 1; ++dy)
            {
                for (int dx = -1; dx <= 1; ++dx)
                {
                    if (dx == 0 && dy == 0) continue;
                    Cell next{current.cell.x + dx, current.cell.y + dy, current.cell.level};
                    if (std::abs(next.x - m_start.x) > m_maxRadius || std::abs(next.y - m_start.y) > m_maxRadius)
                    {
                        m_radiusReached = true;
                        continue;
                    }
                    // A diagonal step is taken only where both straight steps beside it are
                    // free too, so that no corner is cut.
                    if (dx != 0 && dy != 0)
                    {
                        Cell beside{current.cell.x + dx, current.cell.y, current.cell.level};
                        Cell across{current.cell.x, current.cell.y + dy, current.cell.level};
                        if (!passable(current.cell, beside) || !passable(current.cell, across)) continue;
                    }
                    if (!passable(current.cell, next)) continue;
                    if (m_closed.count(next)) continue;
                    const double g = current.g + ((dx != 0 && dy != 0) ? kDiagonal : 1.0);
                    const auto known = m_best.find(next);
                    if (known != m_best.end() && known->second <= g) continue;
                    m_best[next] = g;
                    m_cameFrom[next] = current.cell;
                    m_open.push(Open{g + Heuristic(next), g, next});
                }
            }
            if (!m_links) continue;
            m_linked.clear();
            m_links(current.cell, m_linked);
            for (const Link& link : m_linked)
            {
                const Cell next = link.to;
                if (std::abs(next.x - m_start.x) > m_maxRadius || std::abs(next.y - m_start.y) > m_maxRadius)
                {
                    m_radiusReached = true;
                    continue;
                }
                if (m_closed.count(next)) continue;
                const double g = current.g + link.cost;
                const auto known = m_best.find(next);
                if (known != m_best.end() && known->second <= g) continue;
                m_best[next] = g;
                m_cameFrom[next] = current.cell;
                m_open.push(Open{g + Heuristic(next), g, next});
            }
        }
        return m_state;
    }

    std::vector<Cell> Search::PathTo(Cell end) const
    {
        std::vector<Cell> path;
        Cell at = end;
        path.push_back(at);
        for (int guard = 0; at != m_start && guard < 100000; ++guard)
        {
            const auto from = m_cameFrom.find(at);
            if (from == m_cameFrom.end()) break;
            at = from->second;
            path.push_back(at);
        }
        std::reverse(path.begin(), path.end());
        return path;
    }

    std::vector<Cell> Search::Path() const
    {
        if (m_state != State::Found) return {};
        return PathTo(m_reached);
    }

    std::vector<Cell> Search::PartialPath() const
    {
        return PathTo(m_state == State::Found ? m_reached : m_nearest);
    }

    Flood::Flood(Cell start, int maxRadius, int maxExpansions) : m_start(start), m_maxRadius(maxRadius), m_maxExpansions(maxExpansions)
    {
        m_best[start] = 0.0;
        m_open.push(Open{0.0, start});
    }

    std::optional<int> MirroredLanding(const KnownStep& known, Cell a, int bx, int by)
    {
        if (!known) return std::nullopt;
        for (const int level : {a.level, a.level + 1, a.level - 1})
        {
            if (const auto back = known(Cell{bx, by, level}, a.x, a.y); back && *back == a.level) return level;
        }
        return std::nullopt;
    }

    Arrival PickArrival(const Flood& flood, Cell goal, int reach, const std::function<bool(Cell)>& deep, const std::function<bool(Cell)>& touches,
                        const std::function<bool(Cell)>& off)
    {
        Arrival a;
        const auto outside = [&](Cell c) { return !off || !off(c); };
        if (deep && flood.BestWhere(goal, reach, -1, [&](Cell c) { return deep(c) && outside(c); }, a.cell))
        {
            a.found = true;
            a.deep = true;
            return a;
        }
        if (flood.BestWhere(goal, reach, -1, [&](Cell c) { return touches(c) && outside(c); }, a.cell))
        {
            a.found = true;
            return a;
        }
        if (off && flood.BestWhere(goal, reach, -1, touches, a.cell))
        {
            a.found = true;
            a.offInside = true;
        }
        return a;
    }

    Flood::State Flood::Step(int steps, const Passable& passable)
    {
        while (m_state == State::Running && steps-- > 0)
        {
            if (m_open.empty())
            {
                m_state = State::Complete;
                break;
            }
            if (m_expanded >= m_maxExpansions)
            {
                m_state = State::Budget;
                break;
            }
            const Open current = m_open.top();
            m_open.pop();
            if (m_closed.count(current.cell)) continue;
            m_closed.insert(current.cell);
            m_order.push_back(current.cell);
            m_columns[{current.cell.x, current.cell.y}].push_back(current.cell.level);
            m_frontier = current.g;
            ++m_expanded;
            for (int dy = -1; dy <= 1; ++dy)
            {
                for (int dx = -1; dx <= 1; ++dx)
                {
                    if (dx == 0 && dy == 0) continue;
                    Cell next{current.cell.x + dx, current.cell.y + dy, current.cell.level};
                    if (std::abs(next.x - m_start.x) > m_maxRadius || std::abs(next.y - m_start.y) > m_maxRadius)
                    {
                        m_radiusReached = true;
                        continue;
                    }
                    // A step into a cell already reached takes the run nowhere new: where it lands
                    // known, it is not asked (SetLanding).
                    if (int land = 0; m_landing && m_landing(current.cell, next.x, next.y, land) && m_closed.count(Cell{next.x, next.y, land})) continue;
                    // A diagonal step only where both straight steps beside it are free too, so
                    // that no corner is cut: the steps of Search.
                    if (dx != 0 && dy != 0)
                    {
                        Cell beside{current.cell.x + dx, current.cell.y, current.cell.level};
                        Cell across{current.cell.x, current.cell.y + dy, current.cell.level};
                        if (!passable(current.cell, beside) || !passable(current.cell, across)) continue;
                    }
                    if (!passable(current.cell, next)) continue;
                    if (m_closed.count(next)) continue;
                    const double g = current.g + ((dx != 0 && dy != 0) ? kDiagonal : 1.0);
                    const auto known = m_best.find(next);
                    if (known != m_best.end() && known->second <= g) continue;
                    m_best[next] = g;
                    m_cameFrom[next] = current.cell;
                    m_open.push(Open{g, next});
                }
            }
            if (!m_links) continue;
            m_linked.clear();
            m_links(current.cell, m_linked);
            for (const Link& link : m_linked)
            {
                const Cell next = link.to;
                if (std::abs(next.x - m_start.x) > m_maxRadius || std::abs(next.y - m_start.y) > m_maxRadius)
                {
                    m_radiusReached = true;
                    continue;
                }
                if (m_closed.count(next)) continue;
                const double g = current.g + link.cost;
                const auto known = m_best.find(next);
                if (known != m_best.end() && known->second <= g) continue;
                m_best[next] = g;
                m_cameFrom[next] = current.cell;
                m_open.push(Open{g, next});
            }
        }
        return m_state;
    }

    double Flood::Cost(Cell c) const
    {
        if (!m_closed.count(c)) return -1.0;
        const auto known = m_best.find(c);
        return known == m_best.end() ? -1.0 : known->second;
    }

    std::vector<Cell> Flood::PathTo(Cell end) const
    {
        std::vector<Cell> path;
        if (!m_closed.count(end)) return path;
        Cell at = end;
        path.push_back(at);
        for (int guard = 0; at != m_start && guard < 1000000; ++guard)
        {
            const auto from = m_cameFrom.find(at);
            if (from == m_cameFrom.end()) break;
            at = from->second;
            path.push_back(at);
        }
        std::reverse(path.begin(), path.end());
        return path;
    }

    bool Flood::BestIn(Cell goal, double radius, int levels, Cell& out) const
    {
        const int reach = static_cast<int>(std::ceil(radius));
        double best = -1.0;
        for (int y = goal.y - reach; y <= goal.y + reach; ++y)
        {
            for (int x = goal.x - reach; x <= goal.x + reach; ++x)
            {
                const auto column = m_columns.find({x, y});
                if (column == m_columns.end()) continue;
                if (Distance(Cell{x, y, 0}, Cell{goal.x, goal.y, 0}) > radius) continue;
                for (const int level : column->second)
                {
                    if (levels >= 0 && std::abs(level - goal.level) > levels) continue;
                    const Cell c{x, y, level};
                    const double cost = Cost(c);
                    if (cost >= 0.0 && (best < 0.0 || cost < best))
                    {
                        best = cost;
                        out = c;
                    }
                }
            }
        }
        return best >= 0.0;
    }

    bool Flood::BestWhere(Cell goal, int reach, int levels, const std::function<bool(Cell)>& fits, Cell& out) const
    {
        double best = -1.0;
        for (int y = goal.y - reach; y <= goal.y + reach; ++y)
        {
            for (int x = goal.x - reach; x <= goal.x + reach; ++x)
            {
                const auto column = m_columns.find({x, y});
                if (column == m_columns.end()) continue;
                for (const int level : column->second)
                {
                    if (levels >= 0 && std::abs(level - goal.level) > levels) continue;
                    const Cell c{x, y, level};
                    const double cost = Cost(c);
                    if (cost < 0.0 || (best >= 0.0 && cost >= best) || !fits(c)) continue;
                    best = cost;
                    out = c;
                }
            }
        }
        return best >= 0.0;
    }

    Cell Flood::Nearest(Cell goal, double levelWeight) const
    {
        Cell nearest = m_start;
        double best = Distance(m_start, goal) + levelWeight * std::abs(m_start.level - goal.level);
        for (const Cell& c : m_closed)
        {
            const double d = Distance(c, goal) + levelWeight * std::abs(c.level - goal.level);
            if (d < best)
            {
                best = d;
                nearest = c;
            }
        }
        return nearest;
    }

    Straightener::Straightener(std::vector<Cell> path, size_t maxRun) : m_path(std::move(path)), m_maxRun(std::max<size_t>(1, maxRun))
    {
        if (m_path.empty())
        {
            m_done = true;
            return;
        }
        m_corners.push_back(m_path.front());
        m_candidate = std::min(m_path.size() - 1, m_anchor + m_maxRun);
    }

    bool Straightener::Step(int checks, const std::function<bool(Cell, Cell)>& clear)
    {
        while (!m_done)
        {
            if (m_anchor + 1 >= m_path.size())
            {
                m_done = true;
                break;
            }
            // The next cell always follows: it was stepped into from the anchor.
            if (m_candidate <= m_anchor + 1)
            {
                m_anchor += 1;
                m_corners.push_back(m_path[m_anchor]);
                m_candidate = std::min(m_path.size() - 1, m_anchor + m_maxRun);
                continue;
            }
            if (checks-- <= 0) break;
            if (clear(m_path[m_anchor], m_path[m_candidate]))
            {
                m_anchor = m_candidate;
                m_corners.push_back(m_path[m_anchor]);
                m_candidate = std::min(m_path.size() - 1, m_anchor + m_maxRun);
            }
            else
            {
                --m_candidate;
            }
        }
        return m_done;
    }

    std::vector<Cell> Corners(const std::vector<Cell>& path, const std::function<bool(Cell, Cell)>& clear, size_t maxRun)
    {
        Straightener straightener(path, maxRun);
        while (!straightener.Step(1000000, clear))
        {
        }
        return straightener.Corners();
    }
}
