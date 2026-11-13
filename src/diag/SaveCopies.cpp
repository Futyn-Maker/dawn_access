#include "diag/SaveCopies.hpp"

#include "core/Config.hpp"
#include "core/GameThread.hpp"
#include "core/Log.hpp"
#include "diag/Diagnostics.hpp"
#include "diag/SaveCopyRules.hpp"

#include <shlobj.h>
#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <vector>

namespace da::diag
{
    namespace
    {
        constexpr double kEvery = 2.0;              // seconds between looks at the save
        constexpr size_t kKept = 60;                // the newest copies kept
        constexpr uint64_t kLargest = 256ull << 20; // a save larger than this is not read
        savecopy::Watch g_watch;
        double g_lookedAt = -100.0;
        std::wstring g_label;    // the act of the map last loaded
        std::wstring g_savePath; // the game's one save, found once
        bool g_failedLogged = false;

        // The game's save: Documents\My Games\Bates\Saved\SaveGames\<account>\SaveGame0.sav, the account's
        // folder whose save was written last.
        std::wstring FindSave()
        {
            PWSTR documents = nullptr;
            if (FAILED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &documents)) || !documents) return {};
            const std::filesystem::path root = std::filesystem::path(documents) / L"My Games" / L"Bates" / L"Saved" / L"SaveGames";
            CoTaskMemFree(documents);
            std::error_code ec;
            std::filesystem::path best;
            std::filesystem::file_time_type bestTime{};
            for (const auto& dir : std::filesystem::directory_iterator(root, ec))
            {
                std::error_code e;
                const auto file = dir.path() / L"SaveGame0.sav";
                if (!std::filesystem::is_regular_file(file, e)) continue;
                const auto time = std::filesystem::last_write_time(file, e);
                if (e) continue;
                if (best.empty() || time > bestTime)
                {
                    best = file;
                    bestTime = time;
                }
            }
            return best.wstring();
        }

        savecopy::Stamp StampOf(const std::wstring& path)
        {
            WIN32_FILE_ATTRIBUTE_DATA data{};
            if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return {};
            savecopy::Stamp stamp;
            stamp.size = (static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
            stamp.written = (static_cast<uint64_t>(data.ftLastWriteTime.dwHighDateTime) << 32) | data.ftLastWriteTime.dwLowDateTime;
            return stamp;
        }

        // The whole save, read without standing in the game's way: every share is allowed, so the game can
        // write or replace its save while it is read, and a copy torn so is dropped by the caller.
        bool ReadAll(const std::wstring& path, uint64_t size, std::vector<char>& out)
        {
            if (size == 0 || size > kLargest) return false;
            HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                      FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE) return false;
            out.resize(static_cast<size_t>(size));
            size_t done = 0;
            bool ok = true;
            while (done < out.size())
            {
                DWORD got = 0;
                const DWORD want = static_cast<DWORD>(std::min<size_t>(out.size() - done, size_t{1} << 20));
                if (!ReadFile(file, out.data() + done, want, &got, nullptr) || got == 0)
                {
                    ok = false;
                    break;
                }
                done += got;
            }
            CloseHandle(file);
            return ok && done == out.size();
        }

        std::wstring TimeStamp()
        {
            const std::time_t now = std::time(nullptr);
            std::tm local{};
            localtime_s(&local, &now);
            wchar_t text[32] = {};
            std::wcsftime(text, 32, L"%Y%m%d-%H%M%S", &local);
            return text;
        }

        void Copy(const savecopy::Stamp& stamp)
        {
            std::vector<char> bytes;
            // Read while the game wrote, or not to be read: looked at again at the next poll.
            if (!ReadAll(g_savePath, stamp.size, bytes) || !(StampOf(g_savePath) == stamp)) return;
            const std::wstring dumps = DumpsDirectory();
            if (dumps.empty()) return;
            std::error_code ec;
            const auto folder = std::filesystem::path(dumps) / L"saves";
            std::filesystem::create_directories(folder, ec);
            // Named by the section the save stands in, from the save itself (the map is one for every act).
            const std::wstring section = savecopy::SaveLabel(bytes);
            const auto target = folder / savecopy::CopyName(TimeStamp(), section.empty() ? g_label : section);
            {
                std::ofstream out(target, std::ios::binary | std::ios::trunc);
                out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
                if (!out.good())
                {
                    if (!g_failedLogged) log::Error(L"saves: the copy of the game's save could not be written to {}", target.wstring());
                    g_failedLogged = true;
                    return;
                }
            }
            g_watch.Copied(stamp);
            log::Info(L"saves: the game's save ({} bytes) kept as {}", stamp.size, target.wstring());
            std::vector<std::wstring> names;
            for (const auto& entry : std::filesystem::directory_iterator(folder, ec))
            {
                if (entry.path().extension() == L".sav") names.push_back(entry.path().filename().wstring());
            }
            for (const auto& old : savecopy::Pruned(names, kKept))
                std::filesystem::remove(folder / old, ec);
        }

        void Poll(float)
        {
            // A developer's copies, off unless [Diag] SaveCopies turns them on.
            if (!cfg::Get().saveCopies) return;
            const double now = gamethread::NowSeconds();
            if (now - g_lookedAt < kEvery) return;
            g_lookedAt = now;
            if (g_savePath.empty())
            {
                g_savePath = FindSave();
                if (g_savePath.empty()) return;
                log::Info(L"saves: the game's save is {}; each checkpoint it writes is copied to the dumps' saves folder", g_savePath);
            }
            const auto stamp = StampOf(g_savePath);
            if (g_watch.Due(stamp)) Copy(stamp);
        }
    }

    void InstallSaveCopies()
    {
        gamethread::AddPoller(L"savecopies", &Poll);
    }

    void NoteMap(const std::wstring& mapPath)
    {
        g_label = savecopy::MapLabel(mapPath);
        log::Info(L"map loaded: {}", mapPath);
    }
}
