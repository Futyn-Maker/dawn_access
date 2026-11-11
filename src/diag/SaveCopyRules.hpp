#pragma once
// When the game's save is copied aside, worked out without the game so that it can be tested. The game
// keeps one save slot and writes it at each checkpoint, so a fix to something already played could only
// be tried again by playing up to it anew: in session 70 Andrey took the selfie with Jessica badly before
// he knew how it worked, and the game had saved past it within minutes (2026-11-11). A copy of every
// checkpoint, named by its time and its act, lets any of them be put back with the game closed
// (scripts\save-restore.ps1 -Checkpoint).

#include <cstdint>
#include <string>
#include <vector>

namespace da::savecopy
{
    // A file's size and last write, as the file system tells them; written 0 for no file.
    struct Stamp
    {
        uint64_t size = 0;
        uint64_t written = 0;
        bool operator==(const Stamp&) const = default;
        bool Known() const
        {
            return written != 0;
        }
    };

    // The save as each poll finds it. The game writes a checkpoint in one go, and a copy read while it
    // writes would be torn: a save is copied once it has stood unchanged from one poll to the next, and
    // only when it differs from the last one copied. The save found as the game starts is copied too: it is
    // where the last session ended.
    class Watch
    {
    public:
        // The poll's reading; true when the save is to be copied now.
        bool Due(const Stamp& now);
        // The copy was made of a save that stood unchanged across the read.
        void Copied(const Stamp& copied)
        {
            m_copied = copied;
        }

    private:
        Stamp m_copied;
        Stamp m_seen;
    };

    // A copy's file name: "<stamp>-<label>.sav", the label kept to letters, digits, '_' and '-' and cut to
    // 48 characters; "<stamp>.sav" without one.
    std::wstring CopyName(const std::wstring& stamp, const std::wstring& label);

    // The act a map's path names, for the copy's label: the last part of "/Game/Levels/game/Act2_2/..."
    // that names a level's folder or the map itself, without the "LV_" prefix and "_Persistent".
    std::wstring MapLabel(const std::wstring& mapPath);

    // The section the save stands in, from the save itself: its LevelStreamingSectionName property
    // ("lv_act_2_2_section_03") without the "lv_" prefix, or empty when the bytes hold none. The game
    // streams every act into one map (/Game/levels/game/SingleUberLevel), so the map names nothing: the
    // copies of session 71 were all labelled "SingleUberLevel".
    std::wstring SaveLabel(const std::vector<char>& bytes);

    // Of the copies' names ("<yyyymmdd-hhmmss>-<label>.sav"), the ones to let go: all but the newest
    // `keep` and the first copy of each label, so that the start of every section stays at hand after
    // a long session (session 71's sixty newest reached back only an hour).
    std::vector<std::wstring> Pruned(std::vector<std::wstring> names, size_t keep);
}
