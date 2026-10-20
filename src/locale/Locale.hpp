#pragma once
// Mod-authored strings, localized per game language.
// Tables live in Mods\DawnAccess\lang\<code>.ini ([strings] key=value); every folder below
// lang holds one more table of the same shape, such as the descriptions of the totem visions
// in Mods\DawnAccess\lang\totems\<code>.ini. en_US.ini is always loaded as the fallback; the
// current language overlays it.

#include <string>
#include <string_view>
#include <vector>

namespace da::locale
{
    // Loads en_US plus the given locale code (e.g. "ru_RU"). Returns false when even
    // en_US could not be loaded. Missing keys in the overlay are logged once.
    bool LoadTables(const std::wstring& langDir, std::wstring_view localeCode);

    std::wstring CurrentCode();

    // The mod's language code for a culture as the engine names it ("ru", "es-419",
    // "zh-Hans", "pt-BR"), or for a code already in the mod's own spelling ("ru_RU"): one
    // of the twenty tables, or empty for a culture the game does not have.
    std::wstring CodeForCulture(std::wstring_view culture);

    // Returns the localized string for a key, or the key itself in brackets when
    // it is missing everywhere (so mistakes are audible and visible in the log).
    std::wstring Mod(std::wstring_view key);
    std::wstring Mod(std::wstring_view key, const std::vector<std::wstring>& args);
    std::wstring Mod(std::wstring_view key, std::wstring_view arg0);
    std::wstring Mod(std::wstring_view key, std::wstring_view arg0, std::wstring_view arg1);

    bool Has(std::wstring_view key);
    size_t Count();
    std::vector<std::wstring> MissingKeys();
}
