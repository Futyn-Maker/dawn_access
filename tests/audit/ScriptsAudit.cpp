// Prints every question the scenes' state machines ask about a character in a volume, as
// features/SceneScripts judges it, for the Lua scripts under a folder: one line per question,
// tab-separated (script, machine, state, character, volume, negated, waits, leaves, moves the
// scene on, the calls on the way on). Run before and after a change to the parse, the two
// outputs diffed, to see which waits the change judges otherwise across the whole game.
//
// scripts_audit <folder of the game's exported Lua scripts>
#include "features/SceneScripts.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace da;

namespace
{
    std::wstring ReadWide(const std::filesystem::path& path)
    {
        std::ifstream in(path, std::ios::binary);
        const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        std::wstring out;
        out.reserve(bytes.size());
        for (unsigned char c : bytes)
            out.push_back(static_cast<wchar_t>(c)); // the exports are ASCII
        return out;
    }

    std::string Narrow(const std::wstring& w)
    {
        std::string out;
        for (wchar_t c : w)
            out.push_back(c < 128 ? static_cast<char>(c) : '?');
        return out;
    }
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: scripts_audit <folder>\n");
        return 2;
    }
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(argv[1]))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".lua") files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());
    // The questions of each act's scripts together, as the game runs one act's at a time: a
    // companion sent on is weighed against the waits of its own act (scripts::WeighCompanions).
    std::vector<std::pair<std::wstring, std::vector<scripts::Poll>>> acts;
    std::vector<std::vector<scripts::EndWait>> ends; // each act's waits for a scene to be done
    std::vector<scripts::Leading> leads;             // where a companion leads the player, in every act
    for (const auto& file : files)
    {
        const auto script = ReadWide(file);
        const auto relative = std::filesystem::relative(file, argv[1]);
        const auto source = relative.wstring();
        const auto act = relative.begin() != relative.end() ? relative.begin()->wstring() : std::wstring();
        if (acts.empty() || acts.back().first != act)
        {
            acts.emplace_back(act, std::vector<scripts::Poll>{});
            ends.emplace_back();
        }
        auto waits = scripts::ParseEndWaits(script, source);
        auto led = scripts::ParseLeads(script, source);
        leads.insert(leads.end(), led.begin(), led.end());
        ends.back().insert(ends.back().end(), waits.begin(), waits.end());
        auto polls = scripts::ParsePolls(script, source);
        acts.back().second.insert(acts.back().second.end(), polls.begin(), polls.end());
    }
    for (size_t a = 0; a < acts.size(); ++a)
    {
        auto& polls = acts[a].second;
        // A scene's end the act's flow waits for moves the story on (scripts::WeighEnds); the questions
        // it turns are marked "ends" in a last column.
        std::vector<bool> before;
        for (const auto& p : polls)
            before.push_back(p.story);
        scripts::WeighEnds(polls, ends[a]);
        scripts::WeighCompanions(polls);
        for (const auto& p : polls)
        {
            std::wstring calls;
            for (const auto& c : p.calls)
                calls += (calls.empty() ? L"" : L" ") + c;
            std::wstring sent;
            for (const auto& c : p.sentOn)
                sent += (sent.empty() ? L"" : L" ") + c;
            const bool byEnd = p.ends && p.story && !before[static_cast<size_t>(&p - polls.data())];
            std::printf("%s\t%s\t%d\t%s\t%s\t%d\t%d\t%d\t%d\t%s\t%s%s\n", Narrow(p.source).c_str(), Narrow(p.machine).c_str(), p.state, Narrow(p.who).c_str(),
                        Narrow(p.trigger).c_str(), p.negated ? 1 : 0, p.waits ? 1 : 0, p.leaves ? 1 : 0, p.story ? 1 : 0, Narrow(calls).c_str(),
                        p.sentOnly ? Narrow(sent).c_str() : "", byEnd ? "\tends" : "");
        }
    }
    // Then each lead: "lead", the script, the machine, the state, the companion.
    for (const auto& l : leads)
        std::printf("lead\t%s\t%s\t%d\t%s\n", Narrow(l.source).c_str(), Narrow(l.machine).c_str(), l.state, Narrow(l.character).c_str());
    return 0;
}
