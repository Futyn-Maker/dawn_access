#pragma once
// A copy of each checkpoint the game saves, kept in Mods\DawnAccess\dumps\saves\ under its time and act
// (savecopy::Watch, CopyName), so that a spot already played can be played again after a fix: with the
// game closed, scripts\save-restore.ps1 -Checkpoint puts a copy back as the game's one save.

#include <string>

namespace da::diag
{
    void InstallSaveCopies();
    // The map the game has loaded (its URL's map), for the copies' names. Called on the game thread.
    void NoteMap(const std::wstring& mapPath);
}
