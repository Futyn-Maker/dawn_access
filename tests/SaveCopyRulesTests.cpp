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
    CHECK(savecopy::Pruned(names, 3) == std::vector<std::wstring>{L"20261111-110000-act2_11.sav", L"20261111-120000-act2_11.sav"});
    CHECK(savecopy::Pruned(names, 5).empty());
}
