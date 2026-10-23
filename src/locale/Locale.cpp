#include "locale/Locale.hpp"

#include "core/Config.hpp"
#include "core/Fault.hpp"
#include "core/Log.hpp"
#include "core/Strings.hpp"

#include <algorithm>
#include <filesystem>
#include <map>
#include <mutex>
#include <set>

namespace da::locale
{
    namespace
    {
        // The folders below the language folder, each a table of its own, in a fixed order.
        std::vector<std::wstring> TableFolders(const std::wstring& langDir)
        {
            std::vector<std::wstring> folders;
            std::error_code ec;
            for (const auto& entry : std::filesystem::directory_iterator(langDir, ec))
            {
                if (entry.is_directory(ec)) folders.push_back(entry.path().filename().wstring());
            }
            std::sort(folders.begin(), folders.end());
            return folders;
        }

        std::mutex g_mutex;
        std::map<std::wstring, std::wstring> g_fallback;
        std::map<std::wstring, std::wstring> g_current;
        std::wstring g_code = L"en_US";
        std::set<std::wstring> g_reportedMissing;
        std::vector<std::wstring> g_missingInOverlay;

        bool LoadTable(const std::wstring& path, std::map<std::wstring, std::wstring>& out)
        {
            cfg::Ini ini;
            std::wstring error;
            if (!ini.Load(path, &error))
            {
                log::Verbose(L"locale: {}", error);
                return false;
            }
            const auto* section = ini.Section(L"strings");
            if (!section) return false;
            for (const auto& [k, v] : *section)
            {
                out[k] = str::UnescapeIni(v);
            }
            return true;
        }

        std::wstring LookupLocked(std::wstring_view key)
        {
            const std::wstring k = str::ToLower(key);
            if (const auto it = g_current.find(k); it != g_current.end()) return it->second;
            if (const auto it = g_fallback.find(k); it != g_fallback.end()) return it->second;
            if (g_reportedMissing.insert(k).second)
            {
                log::Error(L"locale: missing string '{}'", key);
            }
            return L"[" + std::wstring(key) + L"]";
        }
    }

    bool LoadTables(const std::wstring& langDir, std::wstring_view localeCode)
    {
        fault::Guard lock(g_mutex, L"locale");
        g_fallback.clear();
        g_current.clear();
        g_missingInOverlay.clear();
        g_reportedMissing.clear();

        const bool okFallback = LoadTable(langDir + L"\\en_US.ini", g_fallback);
        if (!okFallback)
        {
            log::Error(L"locale: cannot load {}\\en_US.ini", langDir);
        }
        const std::vector<std::wstring> folders = TableFolders(langDir);
        for (const auto& folder : folders)
            LoadTable(langDir + L"\\" + folder + L"\\en_US.ini", g_fallback);
        g_code = L"en_US";
        // A code may come as "ru-RU"; tables are named "ru_RU".
        std::wstring code = str::ReplaceAll(str::Trim(localeCode), L"-", L"_");
        if (!code.empty() && !str::EqualsNoCase(code, L"en_US"))
        {
            if (LoadTable(langDir + L"\\" + code + L".ini", g_current))
            {
                g_code = code;
                for (const auto& folder : folders)
                    LoadTable(langDir + L"\\" + folder + L"\\" + code + L".ini", g_current);
                for (const auto& [k, v] : g_fallback)
                {
                    if (!g_current.contains(k)) g_missingInOverlay.push_back(k);
                }
                if (!g_missingInOverlay.empty())
                {
                    log::Info(L"locale: {} keys missing in {} (English used)", g_missingInOverlay.size(), code);
                }
            }
            else
            {
                log::Info(L"locale: no table for {}, using en_US", code);
            }
        }
        log::Info(L"locale: loaded {} strings, language {}", g_fallback.size(), g_code);
        return okFallback;
    }

    std::wstring CurrentCode()
    {
        fault::Guard lock(g_mutex, L"locale");
        return g_code;
    }

    std::wstring CodeForCulture(std::wstring_view culture)
    {
        // The engine's culture names for the game's twenty languages, and the mod's codes.
        static const std::pair<const wchar_t*, const wchar_t*> kCultures[] = {
            {L"da", L"da_DK"},      {L"de", L"de_DE"},       {L"en", L"en_US"},     {L"es", L"es_ES"},     {L"es-419", L"es_MX"},   {L"es-mx", L"es_MX"},
            {L"fi", L"fi_FI"},      {L"fr", L"fr_FR"},       {L"it", L"it_IT"},     {L"ja", L"ja_JP"},     {L"ko", L"ko_KR"},       {L"nl", L"nl_NL"},
            {L"no", L"nn_NO"},      {L"nb", L"nn_NO"},       {L"nn", L"nn_NO"},     {L"pl", L"pl_PL"},     {L"pt", L"pt_PT"},       {L"pt-br", L"pt_BR"},
            {L"ru", L"ru_RU"},      {L"sv", L"sv_SE"},       {L"tr", L"tr_TR"},     {L"zh", L"zh_CHS"},    {L"zh-hans", L"zh_CHS"}, {L"zh-cn", L"zh_CHS"},
            {L"zh-chs", L"zh_CHS"}, {L"zh-hant", L"zh_CHT"}, {L"zh-tw", L"zh_CHT"}, {L"zh-hk", L"zh_CHT"}, {L"zh-cht", L"zh_CHT"},
        };
        const std::wstring name = str::ToLower(str::ReplaceAll(str::Trim(culture), L"_", L"-"));
        if (name.empty()) return {};
        // The whole name first ("pt-br"), then its language alone ("pt" of "pt-pt").
        for (int pass = 0; pass < 2; ++pass)
        {
            const std::wstring wanted = pass == 0 ? name : name.substr(0, name.find(L'-'));
            for (const auto& [engine, code] : kCultures)
            {
                if (wanted == engine) return code;
            }
            // A code already in the mod's spelling, such as "ru-ru" for ru_RU.
            for (const auto& [engine, code] : kCultures)
            {
                if (str::EqualsNoCase(wanted, str::ReplaceAll(code, L"_", L"-"))) return code;
            }
            if (wanted == name) continue;
        }
        return {};
    }

    std::wstring Mod(std::wstring_view key)
    {
        fault::Guard lock(g_mutex, L"locale");
        return LookupLocked(key);
    }

    std::wstring Mod(std::wstring_view key, const std::vector<std::wstring>& args)
    {
        std::wstring pattern;
        {
            fault::Guard lock(g_mutex, L"locale");
            pattern = LookupLocked(key);
        }
        return str::Format(pattern, args);
    }

    std::wstring Mod(std::wstring_view key, std::wstring_view arg0)
    {
        return Mod(key, std::vector<std::wstring>{std::wstring(arg0)});
    }

    std::wstring Mod(std::wstring_view key, std::wstring_view arg0, std::wstring_view arg1)
    {
        return Mod(key, std::vector<std::wstring>{std::wstring(arg0), std::wstring(arg1)});
    }

    bool Has(std::wstring_view key)
    {
        fault::Guard lock(g_mutex, L"locale");
        const std::wstring k = str::ToLower(key);
        return g_current.contains(k) || g_fallback.contains(k);
    }

    size_t Count()
    {
        fault::Guard lock(g_mutex, L"locale");
        return g_fallback.size();
    }

    std::vector<std::wstring> MissingKeys()
    {
        fault::Guard lock(g_mutex, L"locale");
        return g_missingInOverlay;
    }
}
