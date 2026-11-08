#pragma once
// The game's own scripts keep their state in a Lua state of the running level script
// (ULuaScriptUpdater::RunningScript, a UScript holding the lua_State). The scripts decide by
// their globals what a trigger does when walked into (T_EnteredLounge starts Jessica's lost-phone
// talk only while gVariables.bAllowLostPhone is 1, and sets it to 0), so the mod reads those
// globals with the Lua library the executable carries, as the scripts themselves read them.
//
// Reads happen on the game thread, between frames (the engine tick's post callback), when no
// script runs; the script's own lock is tried, and a read is given up when another thread holds
// it. The library's entry points come from the program database (core/Symbols), so nothing is
// read on another build. The game's Lua keeps its numbers as floats.

#include <string>
#include <string_view>
#include <vector>

namespace da::lua
{
    // A value as the scripts' comparisons see it.
    struct Value
    {
        enum class Kind
        {
            Unknown, // no script runs, another build, the lock taken, or the read faulted
            Nil,
            Number,
            Boolean,
            Other,
        };
        Kind kind = Kind::Unknown;
        double number = 0.0;
        bool boolean = false;
    };

    // A field of a global table of the running script (`table`.`field`, gVariables.bAllowLostPhone).
    // Game thread only.
    Value Global(std::string_view table, std::string_view field);

    // A field of a table kept in a global table (`table`.`field`.`key`): the trigger scripts still to
    // run as gEvents.EnterTrigger.<trigger> (Events.lua: an event whose runs are used up is taken out,
    // and the table of a trigger left with none is nil). Nil also where `table`.`field` is no table:
    // gEvents.EnterTrigger goes once no trigger keeps a script to run. Game thread only.
    Value Nested(std::string_view table, std::string_view field, std::string_view key);

    // The scenes' state machines running now and the state each stands in: the scripts' own record,
    // gStateMachines (StateMachineHandler.lua: each machine's name and currentState). Empty when it
    // cannot be read. Game thread only.
    struct Machine
    {
        std::wstring name;
        int state = 0;
    };
    bool Machines(std::vector<Machine>& out);
}
