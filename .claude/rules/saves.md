---
paths:
    - "tools/saves/**"
    - "scripts/save-*.ps1"
    - "src/diag/SaveCopies*"
    - "src/diag/SaveCopyRules*"
---

# Saves

- **Where they are.** One slot: `%USERPROFILE%\Documents\My Games\Bates\Saved\SaveGames\<steamid>\SaveGame0.sav`, a GVAS `BatesSaveGame` under Steam Cloud. The settings are `Saved\Config\UserSettings.dat` (GVAS `BatesUserSettingsSaveGame`). Never touch either while the game runs.
- **The signature.** `UGameplayStatics::SaveGameToSlotSigned` pads the GVAS bytes with zeros to a multiple of 4, then writes the kind byte 0xEA and `FCrc::MemCrc32(padded, 0xFFFFFFFF)` big-endian, which is `zlib.crc32(file[:-5], 0xFFFFFFFF)`. Any edit re-signs, and the GVAS data ends after the top-level "None" and 4 zero bytes.
- **Act snapshots.**
    - `GameSaveData.GlobalStateSnapshots`, keyed act×10000 + part×100 + node, hold the global state as each act began, written at the first save after a story transition.
    - The chapter select starts the episode-start acts only (`SelectChapter`), at node 1.
    - A new story removes the episode menu until the game is finished again.
- **Restarting an act in the same playthrough** (`tools/saves/restart_act.py`: a report first, then `--write <out>`, re-signed and read back).
    - It uses the game's own fallback: a save whose `LuaStateVersion` differs from the build's (8) becomes a minimal save at node 1 with the act's snapshot. Continue then runs the act's script afresh.
    - What the snapshot does not hold is set back by the tool: the totem and clue trackers' statuses, and the 10 characters' relationship and trait records (each a byte, a value, and the value the status page last showed).
    - Check the relationships against the act's choices before `--relations-seen`.
    - Never edit sizes by hand: the tool grows the enclosing size fields and re-signs.
- **Backups.** `scripts\save-backup.ps1 [-Name]` copies the saves and settings into `research\saves\<date>-<name>`. `scripts\save-restore.ps1` puts the newest backup back, or a named one (`-Backup`), or one of the mod's checkpoint copies (`-Checkpoint <name or its start>`), after backing up the current state. `-Settings` restores the settings too.
    - Steam Cloud may ask at the next start which copy to keep: the local one.
- **Checkpoint copies** (`[Diag] SaveCopies=1`, `diag/SaveCopies`).
    - The mod copies the save at every checkpoint into `<mod dir>\dumps\saves\<date>-<section>.sav`, read with every share allowed, the newest 60 kept and each section's first kept.
    - They take about 10 MB each, so they are off by default.
    - They are the way back to a spot that a fix needs played again.
