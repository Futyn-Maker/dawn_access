---
paths:
    - "src/ui/**"
    - "src/features/{Menus,MenuRules,Hud,TextRules,Subtitles,Credits,CreditsRules,Achievements,AchievementRules}.*"
    - "src/watch/Narration.*"
---

# Menus, HUD, subtitles, documents, credits, achievements

## Menus

- **The focus is the game's own.** BMUI's page reflection system (`BMUIPageReflectionSystem` under the player's `BMUISubsystem`) keeps an `ActiveTree`: `LeafNode.PageObject` is the page in front, and `LeafNode.CachedFocusWidget` the focused control, updated live with navigation.
    - The control is the outermost `BMUIButton` above it (`ui::Interactable`), or the focused user widget itself where the game focuses no button.
    - The engine's own focus agrees with it (the inner button `HasKeyboardFocus`); mouse hover does not count.
- **Pages.**
    - A page is created with the HUD container's widget tree as its outer, has no slot, and belongs to a `BMUIPageStack` (`ParentStack`). The stack's `DisplayedWidget` is the page drawn, and only that page counts as shown.
    - Template widgets inside a Blueprint's widget tree are live objects and are excluded (`obj::IsTemplate`).
    - A page shown anew in the same pooled widget is noticed by `watch::DisplayGeneration`; one whose title or a control's label changes under the same widget is read anew; the same arrival text just said is not said again.
- **Arrival.** An arrival waits 0.3 s for the page to settle, and at most 1.2 s for the focus to land and for the page's stack to display the page. Then it says the tab line, the title, the body, the focused control with its heading, and the prompts.
    - A page with no title of its own takes the name the game's narration announces it by (`narration::PageNameFor`).
    - A question drawn beside a row of buttons ("Start chapter?") is said with the button the focus comes to (`menu::AsksOver`, `TextsWithFocus`).
- **A control** (`ui::Describe`, `ui::Speak`) is label, kind, value, state, place and tip:
    - **Label:** a setting row's `SettingName`.
    - **Value:** a carousel's `GetCurrentEntryText`, a slider's `Text_SliderValue`, a checkbox's `GetValue` with its own `CheckEnabledText`/`CheckDisabledText`. A slider's label excludes its value text.
    - **State:** "unavailable" from `bIsEnabled`, said at either level.
    - **Place:** among the shown, labelled, interactable siblings of the same panel, said only among several.
    - **Heading:** the last text before it that is no control (`WBP_SettingCategory_C`'s `Text_Cat`), said when it changes.
    - **Tip:** the footer, else the page's description panel, taken only while that panel's `Text_SettingName` equals the control's label, since the game leaves the last row's blurb up after Backspace.
    - A changing value is said at most every 0.35 s and once more after 0.15 s at rest.
- **The body and the prompts.**
    - Key caps are never label text. A panel with an interactable child is a list, and its other texts are headings, not body.
    - Texts are deduplicated. Templates with `{0}` are skipped (a countdown is formatted a tick later), and so are texts drawn fainter than 0.6 of the brightest seen on the page.
    - A text gained later is announced once by its digit-free shape. After a focus change only the prompts not yet said on the page are said.
    - An axis prompt names one key per visible cap. A page among tabs adds "{key} and {key} switch tabs", since the game shows only key icons beside its tab headers.
- **`WidgetSwitcher`** draws only its active page, and every walk honours it (`obj::ChildWidgets`): a rebinding row's hidden "Awaiting input" page was once read as its value.
- **Rebinding rows** (`BatesKeyRebindSettingWidget`) are worded by their slots' own `CurrentKey`. The engine's key profile knows nothing of the game's mapping names. While a row waits for a key, every key goes to the game.
- **A wordless control** (a thumbnail, a picture, a slot) is labelled from:
    1. its hidden texts;
    2. its `AccessibleText`;
    3. the picture table by the texture or material behind its image (`picture.<texture>`, `mod/lang/pictures`);
    4. the line the game's own narration queues as it is focused (`narration::LabelFor`, 30 s).
- **The game's narration as a text source** (`watch/Narration`).
    - It is read upstream of its language gate: pre-hooks on `BMTTSSubsystem:QueueTTSLine` and `QueueTTSLineFromTextBlock`, and a poller of `ActiveTTSItem` by PDB offsets for the native callers that bypass reflection.
    - The game narrates one control as one utterance of several queued lines in a frame, the first clearing (`bInClearPrio`). `menu::Utterance` joins them ("Death Totem, 6, Locked").
    - A control the narration names gets no layout heading. A narration that leaves the name out takes the heading drawn over the control's group (`menu::NamedByHeading`).
- **Locked items.** The availability flag is read before any name, and what the locked state does to the name decides the words:
    - where the name is hidden or replaced (a padlock, the locked text in its place, a dimmed silhouette), only the game's locked word (`SMG013_64519`) is said;
    - where the name is drawn greyed, the name and the locked word are said.

    The known pairings:
    - bonus items: `ItemData.bIsUnlocked`, a padlock;
    - clues: `Text_ClueName` shows the locked text;
    - butterflies: a silhouette, and the narration says only the locked text;
    - totem slots: `GetIsEnabled`.

    No flag found means no name is said until one is.

- **The "new" badge** (`BatesUnseenItemWidget`) is a mark said after the name, not a name. The totems page lays a second grid over its slots that draws only the badges, and a focused slot takes its twin's badge.
- **Clues.** A clue's extra details are drawn only once their story flag is set (`ExtraDetailsGlobalStateBool`), and are read only then. The clue page's sticks only turn the model and scroll the text.
- **Status page.** A bar is "name, N%" with "increased" or "decreased" from its `In State` (`UBatesStatusBarWidget` `TargetAnimValue`/`MaxValue` by PDB). Nothing on the page is interactable.
- **The events of the past** sit in the action bar's own slot on the totems page (`WBP_EOTPTimeline_C`), read as a prompt with its key while its play button is enabled.
- **The dialogue history** (`WBP_DialogueHistory_C`) is read on arrival as its title, the newest line and its prompts, then a line at a time with the mod's keys (`menu::HistoryLines`; N or T to older lines, P or H back), the place at the detailed level.
- **The settings' QTE rows** show a live example of the setting. It is silent until the demonstration key plays it (`.claude/rules/mechanics.md`), and the key is named after the game's own words for the row.

## HUD

- **A HUD element** is a user widget directly under `WBP_HUDContainer_Game_C`, or one on its own.
    - An element inside a container that holds a `PageBackgroundContainer` (the front end, the boot screen, the pause container), or inside a `BMUIPage`, belongs to the menus. The pause menu's footer was once read twice as HUD.
    - Elements owned elsewhere (subtitles, prompts, QTE, choices, Don't Move, glints, pages, the action bar, credits) are left to their features.
    - A readout takes only texts drawn at 0.6 opacity or more (`ui::DrawnTexts`).
- **What an element says.**
    - An element says only the lines new since its last reading, and a line that leaves and comes back is new again. A line absent for more than 2 s counts as new, so a blinking clock separator is no new line.
    - A row of text blocks in one box is one line: the camcorder's "0:21".
    - A clock line (`lines::ClockTicks`) is said the first time. After that, three fields are said when the hours or the minutes change; two fields when the first changes, or when it stood still 30 s before changing.
    - An element drawn at no opacity shows nothing, so a document's transcript shown again is read again.
- **The log's first-shown lines.** `hud: <class> first shown as ... says ...` is logged once per class: the discovery log for widgets met for the first time. The unread-text audit (`diag/Audit`) logs `unread:` for a text drawn 4 s that no utterance contains.
- **Cards** (`UBatesHUDUpdateQueueWidget`: butterfly effect, totem, clue; status updates) draw their title and key first and fade the body in 1 to 2 s later. `lines::Card` waits for the body (at most 3 s) and reads the card once, whole. A card taken away before its name is said as it stood.
- **Title cards** (chapter, character introduction, character change) lay out every word at once and fade them in one by one. `lines::TitleFill` holds the prompts while a laid-out word is not yet drawn, at most 3 s.
- **The objective.**
    - The game keeps it for the status page (`UBatesTraitsRelationshipsManager::GetCurrentObjective`, set by the scripts' `SF_SetCurrentObjective`) and draws it nowhere when it changes.
    - The mod says it when it changes, once play runs with no title card up for 1 s (`lines::Objective`).
    - At a turn's opening it waits for the character change card to come and go, or for the hand-over.
- **Words over a film** (`ui::IsVideoText`) are read in the game states of a film, as text. A video text that is no one's speech (a place and a time) is said whatever the subtitle switch (`lines::OnScreenText`).
- **Overlays with fixed decoration.** An overlay whose only text is its own furniture is read by its prompts alone (`lines::FurnitureOnly`), and a line of digits never names a bare key (`lines::NamesBareKey`): the selfie's camera screen draws a static "3" by a stopwatch.
- **The loading screen** says "Loading" and its share.
- **The in-play action bar** is read while no screen is up, each new line once after 0.1 s steady; it drops the menus' `IA_UI_*` prompts that linger after a menu.

## Documents

- **The transcript.** `UBatesReadingWidget` flips `bIsReading` on the read key (E, or X/Square) and fades itself with `BP_Hide`/`BP_Show`, staying listed. A transcript drawn at no opacity shows nothing, and one shown again is read again.
    - "Transcript Display: Default On" shows it at once.
    - The transcript's scroll prompt is read while a document is drawn.
- **Pages.** A page turned interrupts the page before (`lines::Reading`, `speech::Replace`; the page rules in `.claude/rules/input-speech.md`).
    - A clue's card and its page are said together, the card first (`lines::CardFirst`).
    - A page shown again unchanged is left unsaid only while the reader may still be on it and no input came since it went (`lines::LeaveShownAgain`). A comment that hides the transcript mid-page then does not repeat the page.
    - A step of a staged page turn says the new page first, then the prompts.

## Subtitles

- `BatesSubtitlesWidget` holds `Subtitle_0` and `Subtitle_1` (`BatesSubtitleLineWidget`: `Text_CharacterName`, `Text_Dialogue`), read every frame with a two-poll dedupe, said as "Name: text".
- The name is read only while drawn, so it follows the game's Show Name setting. Nothing is read while the game's Subtitles setting is Off.
- F9 switches the reading and keeps the choice in `[Subtitles] Read`; F4 repeats the last line.

## Credits

- The epilogue's `UBatesCreditsManager` runs a queue: the films of the dead (with `CharacterName` and `TimeOfDeath` from the script's `SF_QueueCreditMovie`), then the survivors' interviews (room, tape number, clock), with the roll over them.
- **The roll's line widgets are pooled.** A line is a widget's setting up, known by the roll's `ActiveCreditLine` changing to it, by the container, and by its `CurrentOffset` going back (`credits::LineIds`). Keeping lines by widget kept only the first few dozen of hundreds.
- **The roll moves faster than speech,** so it is read on N or D-pad right only (`credits::Taker`): what came since the last press, all of it the first time, "No new credits" else. "Read credits: key" is said once as the roll begins.
- **The interviews' overlay** (`credits::Overlay`) says the room and the clock at each interview, then the clock only as its hours or minutes change; the tape's number is read by F6 only.
- **A film of the dead** says the character's name resolved from its string key, then its description. The time of death is never drawn (the film widget's name text shows only for a film that fails to open), so it is not said.
- The HUD and the audit leave the credits' widgets alone.

## Achievements

- The game draws nothing when a trophy unlocks; Steam shows its own toast.
- `features/Achievements` polls Steam's flat API in the game's `steam_api64.dll` once a second (`SteamAPI_SteamUserStats_*`: the count, names, states). The first whole reading is the baseline, and each later unlock is said once (`achieve::Watch`).
- The words are the game's own trophy strings in its language: `BATES_TROPHIES_<id>_N` and `_D` in `ST_Localized`, the PC wording (`ACH_`) before the console one (`TRP_`). Steam's words come only where the game has none (`achieve::TextKeys`, `achieve::Pick`), and Steam answers in English, the game's only declared base language.
