// diag/SaveCopyRules: when the game's save is copied aside, how a copy is named, and which are let go.
#include "Support.hpp"
#include "diag/SaveCopyRules.hpp"

#include <string>
#include <vector>

using namespace da;

TEST_CASE("saves: a save is copied once it stands unchanged from one look to the next, and once only")
{
    savecopy::Watch watch;
    const savecopy::Stamp start{2070969, 1000};
    // The save as the game starts: copied at the second look that finds it unchanged.
    CHECK_FALSE(watch.Due(start));
    CHECK(watch.Due(start));
    watch.Copied(start);
    CHECK_FALSE(watch.Due(start));
    // The game writes a checkpoint: the first look finds it changed and waits, the next copies it.
    const savecopy::Stamp checkpoint{2071200, 2000};
    CHECK_FALSE(watch.Due(checkpoint));
    CHECK(watch.Due(checkpoint));
    // A copy torn by a write meanwhile is not taken: the save is copied again at the next look.
    CHECK(watch.Due(checkpoint));
    watch.Copied(checkpoint);
    CHECK_FALSE(watch.Due(checkpoint));
    // A save still being written changes from look to look: not copied until it stands.
    CHECK_FALSE(watch.Due(savecopy::Stamp{100, 3000}));
    CHECK_FALSE(watch.Due(savecopy::Stamp{2000, 3001}));
    CHECK(watch.Due(savecopy::Stamp{2000, 3001}));
    // No save found: nothing.
    CHECK_FALSE(watch.Due(savecopy::Stamp{}));
    CHECK_FALSE(watch.Due(savecopy::Stamp{}));
}

TEST_CASE("saves: a copy is named by its time and act, and the oldest beyond the kept are let go")
{
    CHECK(savecopy::CopyName(L"20261111-121355", L"Act2_2") == L"20261111-121355-Act2_2.sav");
    CHECK(savecopy::CopyName(L"20261111-121355", L"") == L"20261111-121355.sav");
    CHECK(savecopy::CopyName(L"20261111-121355", L"act 2/2: mine") == L"20261111-121355-act22mine.sav");
    CHECK(savecopy::MapLabel(L"/Game/Levels/game/Act2_2/LV_Act_2_2") == L"Act2_2");
    CHECK(savecopy::MapLabel(L"/Game/levels/game/act2_11") == L"act2_11");
    CHECK(savecopy::MapLabel(L"/Game/Maps/LV_Act_2_2_Persistent.LV_Act_2_2_Persistent") == L"Act_2_2");
    CHECK(savecopy::MapLabel(L"").empty());
    const std::vector<std::wstring> names{L"20261111-121500-Act2_2.sav", L"20261111-110000-act2_11.sav", L"20261111-121355-Act2_2.sav",
                                          L"20261111-120000-act2_11.sav", L"20261111-122600-Act2_2.sav"};
    // The first copy of act2_11 stays though it is older than the three kept.
    CHECK(savecopy::Pruned(names, 3) == std::vector<std::wstring>{L"20261111-120000-act2_11.sav"});
    CHECK(savecopy::Pruned(names, 5).empty());
}

namespace
{
    // A tagged string property as the save writes it.
    void PutString(std::vector<char>& out, const std::string& text)
    {
        const int32_t length = static_cast<int32_t>(text.size() + 1);
        out.insert(out.end(), reinterpret_cast<const char*>(&length), reinterpret_cast<const char*>(&length) + 4);
        out.insert(out.end(), text.begin(), text.end());
        out.push_back('\0');
    }
    void PutInt(std::vector<char>& out, int32_t value)
    {
        out.insert(out.end(), reinterpret_cast<const char*>(&value), reinterpret_cast<const char*>(&value) + 4);
    }
    std::vector<char> SaveWithSection(const std::string& section, bool guid = false)
    {
        std::vector<char> bytes{'G', 'V', 'A', 'S'};
        PutString(bytes, "SaveTimeStamp");
        PutString(bytes, "StrProperty");
        PutInt(bytes, 24);
        PutInt(bytes, 0);
        bytes.push_back(0);
        PutString(bytes, "2026.11.11-23.46.32");
        PutString(bytes, "LevelStreamingSectionName");
        PutString(bytes, "StrProperty");
        PutInt(bytes, static_cast<int32_t>(section.size() + 5));
        PutInt(bytes, 0);
        bytes.push_back(guid ? 1 : 0);
        if (guid) bytes.insert(bytes.end(), 16, '\x7f');
        PutString(bytes, section);
        PutString(bytes, "None");
        return bytes;
    }
}

TEST_CASE("saves: a copy is named by the section the save itself stands in, and each section's first copy is kept")
{
    // Session 71: the game streams every act into one map, and every copy was "SingleUberLevel".
    CHECK(savecopy::SaveLabel(SaveWithSection("lv_act_2_2_section_03")) == L"act_2_2_section_03");
    CHECK(savecopy::SaveLabel(SaveWithSection("LV_Act_4_2_Section_05", true)) == L"Act_4_2_Section_05");
    CHECK(savecopy::SaveLabel(SaveWithSection("")).empty());
    CHECK(savecopy::SaveLabel(std::vector<char>{'G', 'V', 'A', 'S'}).empty());
    // A long session: the sixty newest reached back an hour; the start of every section stays too.
    const std::vector<std::wstring> names{L"20261111-143000-act_2_2_section_01.sav", L"20261111-143100-act_2_2_section_01.sav",
                                          L"20261111-151000-act_2_2_section_03.sav", L"20261111-151500-act_2_2_section_03.sav",
                                          L"20261111-230000-act_5_1_section_02.sav", L"20261111-234600-act_5_1_section_02.sav"};
    CHECK(savecopy::Pruned(names, 2) == std::vector<std::wstring>{L"20261111-143100-act_2_2_section_01.sav", L"20261111-151500-act_2_2_section_03.sav"});
    CHECK(savecopy::Pruned(names, 6).empty());
}
