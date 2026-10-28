#pragma once
// The words of exploration that need no game to compute: an actor's level name read as
// words, a bearing as one of eight sector words, a distance in metres.

#include <string>
#include <string_view>

namespace da::explore
{
    // The name a thing carries in the level, read as words: the prefix of its kind and the
    // copy number are dropped, and the run-together words are parted where the capitals
    // allow it ("I_note_door" -> "note door", "BP_ExteriorDoor_2" -> "Exterior Door").
    std::wstring NameWords(std::wstring_view objectName);

    // Degrees brought into the range from -180 to 180.
    double NormalizeDegrees(double degrees);

    // The key of the sector word for a bearing in degrees to the right of straight ahead:
    // "dir8.ahead", "dir8.aheadright" and so on round to "dir8.aheadleft".
    const wchar_t* SectorKey(double bearing);

    // Centimetres as whole metres, never less than one.
    std::wstring Metres(double centimetres);
}
