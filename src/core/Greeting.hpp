#pragma once
// What the mod says as it starts, once the game's language is known.

#include <string>

namespace da::greeting
{
    struct Facts
    {
        bool speakOnLoad = true; // [Speech] SpeakOnLoad
        bool detailed = false;   // the verbosity level
        std::wstring version;    // the mod's
        std::wstring reader;     // the screen reader that speaks, or the words for none
        std::wstring language;   // the code of the mod's language
        bool buildKnown = true;  // core/Symbols has a table for the running executable
    };

    // The greeting: briefly that the mod is there, in detail its version, reader and language.
    // A build of the game the mod has no table for is said at either level and even with the
    // greeting switched off, since exploration and aiming do not work on it; nothing else is
    // said when the greeting is off.
    std::wstring Words(const Facts& facts);
}
