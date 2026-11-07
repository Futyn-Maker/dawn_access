#include "features/GroundFile.hpp"

#include <charconv>
#include <cmath>
#include <cstdio>
#include <limits>
#include <sstream>
#include <string_view>

namespace da::ground
{
    namespace
    {
        std::string Num(double v)
        {
            char buffer[64];
            const int n = std::snprintf(buffer, sizeof buffer, "%.2f", v);
            return std::string(buffer, buffer + (n > 0 ? n : 0));
        }

        std::vector<std::string_view> Split(std::string_view line)
        {
            std::vector<std::string_view> out;
            size_t i = 0;
            while (i < line.size())
            {
                while (i < line.size() && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r'))
                    ++i;
                const size_t start = i;
                while (i < line.size() && line[i] != ' ' && line[i] != '\t' && line[i] != '\r')
                    ++i;
                if (i > start) out.push_back(line.substr(start, i - start));
            }
            return out;
        }

        bool ToInt(std::string_view s, int& out)
        {
            return std::from_chars(s.data(), s.data() + s.size(), out).ec == std::errc();
        }

        bool ToDouble(std::string_view s, double& out)
        {
            return std::from_chars(s.data(), s.data() + s.size(), out).ec == std::errc();
        }
    }

    std::string HeaderLine()
    {
        return "ground 1";
    }

    std::string AnchorLine(const Anchor& a)
    {
        return "A " + Num(a.x) + " " + Num(a.y) + " " + Num(a.z) + " " + Num(a.cell) + " " + Num(a.level) + " " + Num(a.half) + " " + Num(a.radius) + " " +
               Num(a.step) + " " + Num(a.walkable);
    }

    std::string ProbeLine(const Probe& p)
    {
        if (p.found)
            return "F " + std::to_string(p.x) + " " + std::to_string(p.y) + " " + std::to_string(p.level) + " " + Num(p.floor) + " " + Num(p.normalZ) +
                   (p.on.empty() ? std::string() : " " + p.on);
        return "N " + std::to_string(p.x) + " " + std::to_string(p.y) + " " + std::to_string(p.level) + " " + std::string(1, p.why ? p.why : 'n');
    }

    std::string StepLine(const Step& s)
    {
        std::string line = "S " + std::to_string(s.from.x) + " " + std::to_string(s.from.y) + " " + std::to_string(s.from.level) + " " + std::to_string(s.bx) +
                           " " + std::to_string(s.by) + " ";
        if (s.free)
            line += std::to_string(s.land) + (s.why == 'M' ? " M " : " - ") + Num(s.clear);
        else
            line += "X " + std::string(1, s.why ? s.why : '?') + " " + Num(s.clear) + (s.on.empty() ? std::string() : " " + s.on);
        return line;
    }

    std::string LevelsLine(const Levels& l)
    {
        std::string line = l.in ? "L in" : "L out";
        for (const auto& name : l.names)
            line += " " + name;
        return line;
    }

    std::string SearchLine(const Search& s)
    {
        return "Q " + std::to_string(s.from.x) + " " + std::to_string(s.from.y) + " " + std::to_string(s.from.level) + " " + std::to_string(s.to.x) + " " +
               std::to_string(s.to.y) + " " + std::to_string(s.to.level) + " " + Num(s.radius) + " " + std::to_string(s.levels);
    }

    std::string ResultLine(const Search& s)
    {
        return std::string("R ") + (s.found ? "found" : "failed") + " " + std::to_string(s.cells) + " " + std::to_string(s.sweeps) + " " +
               std::to_string(s.nearest.x) + " " + std::to_string(s.nearest.y) + " " + std::to_string(s.nearest.level);
    }

    std::string PositionLine(const Position& p)
    {
        return "P " + Num(p.x) + " " + Num(p.y) + " " + Num(p.z);
    }

    std::vector<std::string> VolumeLines(const std::string& name, const shapes::Shape& shape)
    {
        std::vector<std::string> lines;
        const auto point = [](const shapes::Vec& p) { return " " + Num(p.x) + " " + Num(p.y) + " " + Num(p.z); };
        for (const auto& hull : shape.hulls)
        {
            std::string line = "V " + name + " H";
            for (const auto& p : hull.points)
                line += point(p);
            lines.push_back(line);
        }
        for (const auto& ball : shape.balls)
            lines.push_back("V " + name + " B" + point(ball.middle) + " " + Num(ball.radius));
        for (const auto& pill : shape.pills)
            lines.push_back("V " + name + " C" + point(pill.a) + point(pill.b) + " " + Num(pill.radius));
        return lines;
    }

    std::string PassageLine(const ways::Passage& p)
    {
        return "G " + p.name + " " + Num(p.a.x) + " " + Num(p.a.y) + " " + Num(p.a.z) + " " + Num(p.b.x) + " " + Num(p.b.y) + " " + Num(p.b.z) +
               (p.free ? " 1" : " 0");
    }

    bool Record::Parse(const std::string& text)
    {
        bool header = false;
        std::istringstream in(text);
        std::string raw;
        while (std::getline(in, raw))
        {
            const auto f = Split(raw);
            if (f.empty()) continue;
            if (f[0] == "ground")
            {
                header = true;
                continue;
            }
            if (f[0] == "A" && f.size() >= 10)
            {
                Anchor a;
                if (ToDouble(f[1], a.x) && ToDouble(f[2], a.y) && ToDouble(f[3], a.z) && ToDouble(f[4], a.cell) && ToDouble(f[5], a.level) &&
                    ToDouble(f[6], a.half) && ToDouble(f[7], a.radius) && ToDouble(f[8], a.step) && ToDouble(f[9], a.walkable))
                    anchors.push_back(a);
            }
            else if (f[0] == "F" && f.size() >= 6)
            {
                Probe p;
                p.found = true;
                if (ToInt(f[1], p.x) && ToInt(f[2], p.y) && ToInt(f[3], p.level) && ToDouble(f[4], p.floor) && ToDouble(f[5], p.normalZ))
                {
                    if (f.size() >= 7) p.on = std::string(f[6]);
                    probes.push_back(p);
                    if (anchors.empty() || anchors.back().level <= 0.0) continue;
                    floors.emplace(std::tuple<int, int, int>{p.x, p.y, static_cast<int>(std::floor(p.floor / anchors.back().level))}, p.floor);
                }
            }
            else if (f[0] == "T" && f.size() >= 6 && f[5].size() == 8)
            {
                int x = 0, y = 0, level = 0;
                double floor = 0.0;
                if (!ToInt(f[1], x) || !ToInt(f[2], y) || !ToInt(f[3], level) || !ToDouble(f[4], floor)) continue;
                floors[std::tuple<int, int, int>{x, y, level}] = floor;
                size_t k = 0;
                for (int dy = -1; dy <= 1; ++dy)
                {
                    for (int dx = -1; dx <= 1; ++dx)
                    {
                        if (dx == 0 && dy == 0) continue;
                        const char c = f[5][k++];
                        if (c == '.') continue;
                        const int land = c == '-' ? level - 1 : c == '+' ? level + 1 : level;
                        stepLevels[StepKey{x, y, level, x + dx, y + dy}] = land;
                    }
                }
            }
            else if (f[0] == "N" && f.size() >= 5)
            {
                Probe p;
                p.found = false;
                p.why = f[4].empty() ? 'n' : f[4][0];
                if (ToInt(f[1], p.x) && ToInt(f[2], p.y) && ToInt(f[3], p.level)) probes.push_back(p);
            }
            else if (f[0] == "S" && f.size() >= 9)
            {
                Step s;
                if (!ToInt(f[1], s.from.x) || !ToInt(f[2], s.from.y) || !ToInt(f[3], s.from.level) || !ToInt(f[4], s.bx) || !ToInt(f[5], s.by)) continue;
                ToDouble(f[8], s.clear);
                if (f[6] == "X")
                {
                    s.free = false;
                    s.why = f[7].empty() ? '?' : f[7][0];
                    if (f.size() >= 10) s.on = std::string(f[9]);
                }
                else
                {
                    if (!ToInt(f[6], s.land)) continue;
                    s.free = true;
                }
                steps.push_back(s);
                if (s.free) stepLevels[StepKey{s.from.x, s.from.y, s.from.level, s.bx, s.by}] = s.land;
            }
            else if (f[0] == "L" && f.size() >= 2 && (f[1] == "in" || f[1] == "out"))
            {
                Levels l;
                l.in = f[1] == "in";
                for (size_t i = 2; i < f.size(); ++i)
                    l.names.emplace_back(f[i]);
                l.steps = steps.size();
                l.probes = probes.size();
                levels.push_back(std::move(l));
            }
            else if (f[0] == "Q" && f.size() >= 9)
            {
                Search s;
                if (ToInt(f[1], s.from.x) && ToInt(f[2], s.from.y) && ToInt(f[3], s.from.level) && ToInt(f[4], s.to.x) && ToInt(f[5], s.to.y) &&
                    ToInt(f[6], s.to.level) && ToDouble(f[7], s.radius) && ToInt(f[8], s.levels))
                    searches.push_back(s);
            }
            else if (f[0] == "R" && f.size() >= 7)
            {
                if (searches.empty() || searches.back().done) continue;
                Search& s = searches.back();
                s.done = true;
                s.found = f[1] == "found";
                ToInt(f[2], s.cells);
                ToInt(f[3], s.sweeps);
                ToInt(f[4], s.nearest.x);
                ToInt(f[5], s.nearest.y);
                ToInt(f[6], s.nearest.level);
            }
            else if (f[0] == "P" && f.size() >= 4)
            {
                Position p;
                if (ToDouble(f[1], p.x) && ToDouble(f[2], p.y) && ToDouble(f[3], p.z)) positions.push_back(p);
            }
            else if (f[0] == "G" && f.size() >= 9)
            {
                ways::Passage p;
                p.name = std::string(f[1]);
                if (ToDouble(f[2], p.a.x) && ToDouble(f[3], p.a.y) && ToDouble(f[4], p.a.z) && ToDouble(f[5], p.b.x) && ToDouble(f[6], p.b.y) &&
                    ToDouble(f[7], p.b.z))
                {
                    p.free = f[8] == "1";
                    passages.push_back(p);
                }
            }
            else if (f[0] == "V" && f.size() >= 4)
            {
                std::vector<double> n;
                for (size_t i = 3; i < f.size(); ++i)
                {
                    double v = 0.0;
                    if (!ToDouble(f[i], v)) break;
                    n.push_back(v);
                }
                auto& shape = volumes[std::string(f[1])];
                if (f[2] == "H" && n.size() >= 3 && n.size() % 3 == 0)
                {
                    shapes::Hull hull;
                    for (size_t i = 0; i + 2 < n.size(); i += 3)
                        hull.points.push_back({n[i], n[i + 1], n[i + 2]});
                    shape.hulls.push_back(hull);
                }
                else if (f[2] == "B" && n.size() == 4)
                    shape.balls.push_back({{n[0], n[1], n[2]}, n[3]});
                else if (f[2] == "C" && n.size() == 7)
                    shape.pills.push_back({{n[0], n[1], n[2]}, {n[3], n[4], n[5]}, n[6]});
            }
        }
        return header;
    }

    std::optional<int> Record::FreeStep(grid::Cell from, int bx, int by) const
    {
        const auto it = stepLevels.find(StepKey{from.x, from.y, from.level, bx, by});
        if (it == stepLevels.end()) return std::nullopt;
        return it->second;
    }

    grid::Passable Record::Passable(int* unknown, bool mirror) const
    {
        return [this, unknown, mirror](grid::Cell from, grid::Cell& to)
        {
            if (const auto land = FreeStep(from, to.x, to.y))
            {
                to.level = *land;
                return true;
            }
            if (mirror)
            {
                if (const auto back = grid::MirroredLanding([this](grid::Cell f, int x, int y) { return FreeStep(f, x, y); }, from, to.x, to.y))
                {
                    to.level = *back;
                    return true;
                }
            }
            if (unknown && Find(from, to.x, to.y) == nullptr) ++*unknown;
            return false;
        };
    }

    grid::Links Record::Links() const
    {
        if (anchors.empty() || passages.empty()) return {};
        const Anchor a = anchors.back();
        return [this, a](grid::Cell from, std::vector<grid::Link>& out)
        {
            const auto floor = floors.find(std::tuple<int, int, int>{from.x, from.y, from.level});
            if (floor == floors.end()) return;
            // A cell's middle as Exploration places it (WorldOf): half a cell in from its corner.
            const ways::Vec middle{a.x + (from.x + 0.5) * a.cell, a.y + (from.y + 0.5) * a.cell, floor->second};
            ways::PassageLinks(
                passages, middle, a.cell,
                [&](const ways::Vec& end, grid::Cell& cell, ways::Vec& cellMiddle)
                {
                    const int x = static_cast<int>(std::floor((end.x - a.x) / a.cell));
                    const int y = static_cast<int>(std::floor((end.y - a.y) / a.cell));
                    bool found = false;
                    double best = ways::kPassageRise;
                    for (auto it = floors.lower_bound(std::tuple<int, int, int>{x, y, std::numeric_limits<int>::min()});
                         it != floors.end() && std::get<0>(it->first) == x && std::get<1>(it->first) == y; ++it)
                    {
                        const double off = std::fabs(it->second - end.z);
                        if (off > best) continue;
                        best = off;
                        cell = grid::Cell{x, y, std::get<2>(it->first)};
                        cellMiddle = ways::Vec{a.x + (x + 0.5) * a.cell, a.y + (y + 0.5) * a.cell, it->second};
                        found = true;
                    }
                    return found;
                },
                out);
        };
    }

    const Step* Record::Find(grid::Cell from, int bx, int by) const
    {
        for (const auto& s : steps)
        {
            if (s.from == from && s.bx == bx && s.by == by) return &s;
        }
        return nullptr;
    }
}
