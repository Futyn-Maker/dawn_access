// Judges trigger volumes over a ground record the mod wrote in play, as the reach map judges a way
// on: for every volume whose collision lies over the recorded ground, whether the character gets
// to a cell where her capsule touches it (features/Shapes), at what cost on foot, and where the
// box the mod judged volumes by before 2026-11-02 took it for reached instead, and whether she
// touches the volume there. A record holds a stretch of ground per anchor ("A" line); each is
// flooded from its anchor's cell, as the reach map floods from where the character stood.
//
// audit <ground.tsv> <volumes.tsv> [--all]
//
// The volumes are the ground record's own "V" lines (a record written in play holds the ways
// on it judged; research/tools/level_triggers.py writes them for every trigger of the levels),
// with "O name mx my mz sx sy sz yaw" for the old box. --all prints the volumes no stretch
// reaches too.
#include "features/GridPath.hpp"
#include "features/GroundFile.hpp"
#include "features/Shapes.hpp"
#include "features/WayList.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace da;
using shapes::Vec;

namespace
{
    struct OldBox
    {
        Vec middle, size;
        double yaw = 0.0;
    };

    std::string Read(const char* path)
    {
        std::ifstream in(path, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }

    int LevelOf(double z, double level)
    {
        return static_cast<int>(std::floor(z / level));
    }

    // The stretches of a record: the text from each anchor to the next.
    std::vector<std::string> Stretches(const std::string& text)
    {
        std::vector<std::string> out;
        std::istringstream in(text);
        std::string line;
        std::string current;
        while (std::getline(in, line))
        {
            if (line.rfind("A ", 0) == 0)
            {
                if (!current.empty()) out.push_back(current);
                current = "ground 1\n";
            }
            if (!current.empty()) current += line + "\n";
        }
        if (!current.empty()) out.push_back(current);
        return out;
    }
}

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::fprintf(stderr, "audit <ground.tsv> <volumes.tsv> [--all]\n");
        return 2;
    }
    const bool all = argc > 3 && std::string(argv[3]) == "--all";
    const std::string volumesText = Read(argv[2]);
    ground::Record volumesRecord;
    volumesRecord.Parse("ground 1\n" + volumesText);
    std::map<std::string, OldBox> old;
    {
        std::istringstream in(volumesText);
        std::string line;
        while (std::getline(in, line))
        {
            if (line.rfind("O ", 0) != 0) continue;
            std::istringstream f(line.substr(2));
            std::string name;
            OldBox box;
            if (f >> name >> box.middle.x >> box.middle.y >> box.middle.z >> box.size.x >> box.size.y >> box.size.z >> box.yaw) old[name] = box;
        }
    }
    const auto stretches = Stretches(Read(argv[1]));
    std::printf("%zu volumes, %zu stretches of ground\n", volumesRecord.volumes.size(), stretches.size());
    std::map<std::string, std::string> verdicts;
    int stretchIndex = 0;
    for (const auto& text : stretches)
    {
        ++stretchIndex;
        ground::Record record;
        if (!record.Parse(text) || record.anchors.empty()) continue;
        const auto& anchor = record.anchors.front();
        if (record.stepLevels.empty()) continue;
        const auto middle = [&](grid::Cell c)
        {
            const auto floor = record.floors.find({c.x, c.y, c.level});
            const double z = floor != record.floors.end() ? floor->second : c.level * anchor.level + anchor.level / 2.0;
            return Vec{anchor.x + (c.x + 0.5) * anchor.cell, anchor.y + (c.y + 0.5) * anchor.cell, z + anchor.half};
        };
        const grid::Cell start{0, 0, LevelOf(anchor.z - anchor.half, anchor.level)};
        grid::Flood flood(start, 2000, 2000000);
        const auto passable = record.Passable();
        while (flood.Current() == grid::Flood::State::Running)
            flood.Step(4096, passable);
        if (flood.Expanded() < 50) continue;
        for (const auto& [name, shape] : volumesRecord.volumes)
        {
            Vec low, high;
            shape.Bounds(low, high);
            const Vec at{(low.x + high.x) / 2.0, (low.y + high.y) / 2.0, (low.z + high.z) / 2.0};
            const grid::Cell goal{static_cast<int>(std::floor((at.x - anchor.x) / anchor.cell)), static_cast<int>(std::floor((at.y - anchor.y) / anchor.cell)),
                                  LevelOf(at.z, anchor.level)};
            const int reach = static_cast<int>(std::ceil((std::hypot(high.x - low.x, high.y - low.y) / 2.0 + anchor.radius) / anchor.cell)) + 1;
            // Over this stretch only when its ground came near the volume.
            grid::Cell any;
            const bool near = flood.BestWhere(
                goal, reach, -1,
                [&](grid::Cell c)
                {
                    const Vec m = middle(c);
                    return m.x >= low.x - 300.0 && m.x <= high.x + 300.0 && m.y >= low.y - 300.0 && m.y <= high.y + 300.0;
                },
                any);
            if (!near) continue;
            const auto touches = [&](grid::Cell c) { return shapes::CapsuleTouches(shape, middle(c), anchor.radius, anchor.half); };
            grid::Cell touching;
            const bool reached = flood.BestWhere(goal, reach, -1, touches, touching);
            std::string verdict = reached ? "reached, " + std::to_string(static_cast<int>(flood.Cost(touching) * anchor.cell)) + " cm" : "NOT REACHED";
            const auto box = old.find(name);
            if (box != old.end())
            {
                const auto turned = ways::TurnedBox(box->second.middle, box->second.size, box->second.yaw);
                const int boxReach =
                    static_cast<int>(std::ceil((std::max(turned.high.x - turned.low.x, turned.high.y - turned.low.y) / 2.0 + anchor.radius) / anchor.cell)) + 1;
                grid::Cell boxed;
                const auto feet = [&](grid::Cell c)
                {
                    const Vec m = middle(c);
                    return Vec{m.x, m.y, m.z - anchor.half};
                };
                if (flood.BestWhere(goal, boxReach, -1, [&](grid::Cell c) { return ways::CapsuleOverlaps(turned, feet(c), anchor.radius); }, boxed))
                {
                    const Vec m = middle(boxed);
                    const double off =
                        shapes::Distance(shape, Vec{m.x, m.y, m.z - (anchor.half - anchor.radius)}, Vec{m.x, m.y, m.z + (anchor.half - anchor.radius)});
                    verdict += "; old box: " + std::to_string(static_cast<int>(flood.Cost(boxed) * anchor.cell)) + " cm, " +
                               (off <= anchor.radius ? std::string("touches") : "MISSES by " + std::to_string(static_cast<int>(off - anchor.radius)) + " cm");
                }
                else
                {
                    verdict += "; old box: not reached";
                }
            }
            auto& kept = verdicts[name];
            const std::string line = "stretch " + std::to_string(stretchIndex) + ": " + verdict;
            // A volume over several stretches keeps the one that reached it, else the last.
            if (kept.empty() || kept.find("NOT REACHED") != std::string::npos) kept = line;
        }
    }
    int reached = 0, missed = 0, oldMisses = 0;
    for (const auto& [name, verdict] : verdicts)
    {
        if (verdict.find("NOT REACHED") != std::string::npos)
            ++missed;
        else
            ++reached;
        if (verdict.find("MISSES") != std::string::npos) ++oldMisses;
        std::printf("%s\t%s\n", name.c_str(), verdict.c_str());
    }
    if (all)
    {
        for (const auto& [name, shape] : volumesRecord.volumes)
        {
            if (!verdicts.count(name)) std::printf("%s\tno ground recorded near it\n", name.c_str());
        }
    }
    std::printf("over the recorded ground: %d volumes reached where the capsule touches them, %d not; the old box misses %d of them\n", reached, missed,
                oldMisses);
    return 0;
}
