#pragma once
// Development diagnostics and the user-facing "read screen"/"help" readouts.

#include "core/Log.hpp"

#include <string>
#include <string_view>

namespace da::diag
{
    // Writes every visible user widget with its texts and the top-level widgets on display to
    // Mods\DawnAccess\dumps\screen-<timestamp>.txt and to the log.
    void DumpScreen();

    void ToggleTrace();
    // Sets the log level and keeps it in the ini for the next session.
    void SetLogLevel(log::Level level);
    void CycleLogLevel();

    // Composes what F6 says: screen, focused element, prompts, mechanic state, last subtitle.
    std::wstring ReadScreen();
    // The same without the fallbacks for an empty screen: what the features describe and
    // the screen shows, or nothing.
    std::wstring Readout();

    // Composes what F8 says.
    std::wstring Help();

    void SetModDir(const std::wstring& modDir);

    // Developer command file: Mods\DawnAccess\command.txt is polled twice a second; each
    // line is executed and the file is deleted. Commands: "trace on|off", "dump", "loglevel
    // Error|Info|Verbose|Trace", "say <text>", "note <text>" (a line in the log, nothing
    // spoken), "read", "help", "onoff" (the mod's switch, as
    // F1), "props <object path or class name>", "key <input action>", "walkkeys", "objects"
    // (every object with its properties, to the dumps folder), "headers" (C++ headers of
    // every class, likewise), "focus" (every focus signal of the UI, to the log), and the
    // commands the features register.
    void InstallCommandFile();

    // A command of a feature's own: the handler gets what follows the command's name.
    using CommandHandler = void (*)(const std::wstring& args);
    void RegisterCommand(std::wstring_view name, CommandHandler handler);

    // Writes every property of an object to the log (by full path, or the first live
    // instance of a class).
    void DumpProperties(const std::wstring& what);

    // Logs the frame times of every half minute as percentiles, which is where the mod's
    // own cost shows first.
    void InstallFrameTimer();
}
