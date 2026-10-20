#pragma once
// The game's own text: the language the engine shows it in, and the strings of the game's
// string tables by their keys, as the game itself resolves them.

#include <string>
#include <string_view>

namespace da::gametext
{
    // The culture the engine shows its text in, as it names it ("ru", "es-419", "zh-Hans").
    // Empty before the engine answers.
    std::wstring CurrentCulture();

    // The mod's language code for that culture (locale::CodeForCulture): empty before the
    // engine answers, or when the culture is not one of the game's.
    std::wstring CurrentLocale();

    // The game's text for a key of its string tables ("BATES_MENU_QUIT"), in the current
    // language. Empty when no table has the key.
    std::wstring Resolve(std::wstring_view key);

    // Resolves a key, falling back to a non-localized string when the key is empty or unknown.
    std::wstring ResolveWithFallback(std::wstring_view key, std::wstring_view fallback);

    // Drops cached resolutions (when the language changes).
    void ClearCache();
}
