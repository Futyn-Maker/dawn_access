-- A tool for the mod's maintainers, never shipped: writes the reflection mappings (.usmap) of the
-- running build beside UE4SS through UE4SS's own generator, 75 seconds after the game starts and
-- again whenever the Insert key is pressed (so that a dump can be taken once more of the game has
-- loaded: the Blueprint classes of the levels are known only in play). The mod cannot do it
-- itself, as UE4SS does not export its generator to C++ mods. Install this folder as
-- ue4ss\Mods\USMapDump\ (enabled.txt beside Scripts\), play, then remove it again;
-- tools/assets/tojson.py tells how the mappings are used.
local function Dump()
    ExecuteInGameThread(function()
        print("[USMapDump] generating the mappings\n")
        DumpUSMAP(true)
        print("[USMapDump] done\n")
    end)
end
ExecuteWithDelay(75000, Dump)
RegisterKeyBind(Key.INS, Dump)
