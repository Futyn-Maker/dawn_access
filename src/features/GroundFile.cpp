#include "features/GroundFile.hpp"

#include <charconv>
#include <cmath>
#include <cstdio>
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
        if (p.found) return "F " + std::to_string(p.x) + " " + std::to_string(p.y) + " " + std::to_string(p.level) + " " + Num(p.floor) + " " + Num(p.normalZ);
        return "N " + std::to_string(p.x) + " " + std::to_string(p.y) + " " + std::to_string(p.level) + " " + std::string(1, p.why ? p.why : 'n');
    }

    std::string StepLine(const Step& s)
    {
        std::string line = "S " + std::to_string(s.from.x) + " " + std::to_string(s.from.y) + " " + std::to_string(s.from.level) + " " + std::to_string(s.bx) +
                           " " + std::to_string(s.by) + " ";
        if (s.free)
            line += std::to_string(s.land) + " - " + Num(s.clear);
        else
            line += "X " + std::string(1, s.why ? s.why : '?') + " " + Num(s.clear);
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
                }
                else
                {
                    if (!ToInt(f[6], s.land)) continue;
                    s.free = true;
                }
                steps.push_back(s);
                if (s.free) stepLevels[StepKey{s.from.x, s.from.y, s.from.level, s.bx, s.by}] = s.land;
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
        }
        return header;
    }

    grid::Passable Record::Passable(int* unknown) const
    {
        return [this, unknown](grid::Cell from, grid::Cell& to)
        {
            const auto it = stepLevels.find(StepKey{from.x, from.y, from.level, to.x, to.y});
            if (it == stepLevels.end())
            {
                if (unknown && Find(from, to.x, to.y) == nullptr) ++*unknown;
                return false;
            }
            to.level = it->second;
            return true;
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
