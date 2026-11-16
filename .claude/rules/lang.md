---
paths:
    - "mod/lang/*.ini"
    - "mod/lang/rooms/**"
    - "src/locale/**"
    - "tools/lang/**"
---

# Language tables

- **One table for each of the game's 20 text languages,** named by locale code: `da_DK`, `de_DE`, `en_US`, `es_ES`, `es_MX` (the game's Latin American Spanish), `fi_FI`, `fr_FR`, `it_IT`, `ja_JP`, `ko_KR`, `nb_NO`, `nl_NL`, `pl_PL`, `pt_BR`, `pt_PT`, `ru_RU`, `sv_SE`, `tr_TR`, `zh_CHS`, `zh_CHT`.
    - The game's Norwegian is Bokmål, so its table is `nb_NO`, written with the game's own Norwegian words.
    - `locale::CodeForCulture` maps the game's cultures onto these codes, and is tested against the shipped files.
- **The format.** Each file is `[strings]`, then `key=value` lines. `en_US.ini` is the master, and every other file carries exactly its keys. The same holds for every folder under `mod\lang` (`rooms`, `totems`, `films`, `pictures`; `.claude/rules/descriptions.md` for the last three).
    - `scripts\check-lang.ps1` checks every table.
    - A missing key falls back to English and is logged.
- **Placeholders and special values.**
    - `{0}`, `{1}` and so on are filled in by the mod: keep them.
    - "Dawn Access" stays untranslated.
    - The `key.*` entries are the spoken names of keys; `key.ps.*` are the PlayStation names, which win while the game draws PlayStation icons.
    - A value that starts with `@` names one of the game's own strings (`@BATES_GAMEPAD_PS_TOUCHPAD_L`), said in the game's words in every language.
    - A value that starts with `=` takes another entry's words.
- **Write a new string in all 20 languages at once,** in natural wording for each, never left in English to be translated later. `python -I tools/lang/langtable.py` inserts, replaces or appends a key in every table of a folder at once.
- **Words the game itself uses are taken from the game,** in each language, from its string tables (`tools/text/locres.py`):
    - its names of settings, mechanics and screens ("Don't Move", the setting names quoted in help);
    - its place words: room names from its captions and labels ("Found:", "Location:"), never from dialogue, and never from the designers' story labels;
    - its characters' names, as each language writes them (the game's Korean writes Hannah 해나, its Chinese names Ashley and Sam its own way).

    At run time `gametext::Resolve` takes a string from the game's table `ST_Localized` in the current language.

- **Room names** (`mod/lang/rooms/<code>.ini`, `room.<key>`) exist for every room the game's reverb volumes lay out (`rooms::Key` strips the volume's prefix and number).
    - A numbered part laid over another place has its own key with the number kept (`room.kitchen_4`); the lookup tries the numbered key first (`rooms::NameKeys`).
    - A room is named by what is physically there, the game's own place words in each language. A designers' label like "Sams room" names a laundry when the dressing shows sinks and linen baskets.
- **The language is the game's text language.**
    - It is read from the engine's culture once it has settled at start, and followed for the session: a change reloads the mod's tables, clears every cache of resolved game strings and picks the SAPI voice anew. A cache of game strings must never outlive a language.
    - `[General] Language` in the ini can force another table.
