#pragma once
// The names the game's levels give their actors: the name a thing's core properties carry, and the
// name the level's actor database keeps for it, which the level's scripts and activities call it by
// (an activity's stage starts "I_psychsession_1_Interaction"). Either may be empty: Dr. Hill's card
// and Sam's sketchbook set no core name.

#include <string>

namespace RC::Unreal
{
    class UObject;
}

namespace da::level
{
    // The core properties' CoreName (BatesCorePropertiesComponent), whitespace collapsed; empty for none.
    std::wstring CoreName(RC::Unreal::UObject* actor);

    // The actor database's friendly name (LADFunctionLibrary::FindLevelDatabaseByActor, then the
    // database's GetActorData, sublevels searched), whitespace collapsed; empty for none. Asked once per
    // actor, since the answer comes back as a struct whose string and map stay with the pool. Asked on
    // the game thread.
    std::wstring FriendlyName(RC::Unreal::UObject* actor);
}
