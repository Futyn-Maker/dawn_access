// The achievements said as they unlock (features/AchievementRules).

#include "Support.hpp"
#include "features/AchievementRules.hpp"

#include <string>
#include <vector>

using namespace da;

namespace
{
    std::vector<achieve::State> States(std::initializer_list<std::pair<const char*, bool>> list)
    {
        std::vector<achieve::State> states;
        for (const auto& [id, achieved] : list)
            states.push_back(achieve::State{id, achieved});
        return states;
    }
}

TEST_CASE("achievements: what the player had when the mod first read Steam is never said; each unlock after it once")
{
    achieve::Watch watch;
    // Steam not ready: no achievements, or a reading that missed some: nothing seeded, nothing said.
    CHECK(watch.Update({}, true).empty());
    CHECK(watch.Update(States({{"CarnivalOfSolitude", true}}), false).empty());
    CHECK_FALSE(watch.Seeded());
    // The first whole reading: the player's achievements so far, none of them said (Steam's toast shows
    // only one unlocked now).
    CHECK(watch.Update(States({{"CarnivalOfSolitude", true}, {"Survivor", false}, {"Hunter", false}}), true).empty());
    CHECK(watch.Seeded());
    CHECK(watch.Achieved() == 1);
    // Unchanged: nothing.
    CHECK(watch.Update(States({{"CarnivalOfSolitude", true}, {"Survivor", false}, {"Hunter", false}}), true).empty());
    // One unlocked: it, once.
    CHECK(watch.Update(States({{"CarnivalOfSolitude", true}, {"Survivor", true}, {"Hunter", false}}), true) == std::vector<std::string>{"Survivor"});
    CHECK(watch.Update(States({{"CarnivalOfSolitude", true}, {"Survivor", true}, {"Hunter", false}}), true).empty());
    // A reading that missed one still says what it read as unlocked, and forgets nothing.
    CHECK(watch.Update(States({{"Hunter", true}}), false) == std::vector<std::string>{"Hunter"});
    CHECK(watch.Update(States({{"CarnivalOfSolitude", true}, {"Survivor", true}, {"Hunter", true}}), true).empty());
    // Taken back by Steam (stats reset) and unlocked again: said again, as Steam's toast shows it again.
    CHECK(watch.Update(States({{"CarnivalOfSolitude", true}, {"Survivor", false}, {"Hunter", true}}), true).empty());
    CHECK(watch.Update(States({{"CarnivalOfSolitude", true}, {"Survivor", true}, {"Hunter", true}}), true) == std::vector<std::string>{"Survivor"});
}
