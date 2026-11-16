---
paths:
    - "mod/lang/totems/**"
    - "mod/lang/films/**"
    - "mod/lang/pictures/**"
    - "src/features/{PictureRules,Films}.*"
---

# Descriptions of pictures and films

The first part holds for any game; the second is how this mod keeps and times them.

## What is described

- **A picture or a film is described when it shows a sighted player something the game gives in no words.** That covers:
    - visions and other films: in the world, on screens, full screen, flashbacks, the films of the dead, an opening film;
    - photos, cards, drawings, maps, posters, monitors;
    - every object the game shows the player to look at, held up, peeked at or examined.

    A look where a character speaks is no exception: an interjection, or a comment that does not say what the thing looks like, leaves the picture to describe.

- **Never described:**
    - what the game reads out itself: documents, notes, subtitles, the words a film burns in that the game gives as text;
    - a clue the game describes in its own menu, except a story detail its words leave out, never looks alone (see Clues below);
    - real-time scenes played by the engine, whose information is their speech and their subtitles;
    - films that never play;
    - plain pickups and actions (weapons, doors, levers), which are no looks.
- **Text drawn on a thing** (a sign, a label, a plate, writing on its back) is quoted verbatim inside the description, in the language the game draws it in: English stays English in every table. A broken sign is quoted as it reads ("GER / EDGE").
- **One picture shown in two places** has one text, and the other key refers to it.
- **A thing the story draws in several ways** has one text per way the player can see, picked by the game's own record: a scarecrow's head chosen by an earlier answer, a doll set by the choices made, a censored cut.
    - A difference the description leaves out (a character's clothes, a missing finger) falls back to the base text.
    - A censored cut that shows less gets a text of its own.
- **Untaken branches** are described like the rest, but only from certain evidence. No description is better than a false one: when the frame cannot be established, leave the picture undescribed and record what is missing.

## Evidence

- **First the game's own data:**
    - a film's frames, decoded at full resolution;
    - a texture decoded from the cooked asset;
    - a mesh's UV layout, to tell which part of an atlas sits on which page or side;
    - a material's parameters and their tracks, to tell which slice a flickering monitor shows and when.

    These settle what the picture holds, and when in time.

- **Footage of play** only where the asset cannot settle it, such as what a camera frames at run time. Offline projections of a generic examine camera are no evidence: they framed the wrong thing twice where footage showed otherwise.
- **A machine description is a draft at most.** A vision model's account of a film invents sounds and details. Every claim is checked against the frames, and the decisive points against the highest-resolution source, where details like a pair of glasses or a word on a cross only show.
- **Every placement is checked:** one class placed in several levels may show different things in each (a door with its knob in one level and without it in another), and the description is held to the level that places it.
- **Review:** the master texts are reviewed frame by frame by reviewers who were not given the author's conclusions, before translation.

## Wording

- **Dry, factual, present tense.** Say what is visible, in the order the eye takes it: who or what, where in the frame, what happens.
    - No interpretation, no mood, no sound: the films' sound is heard, and a description is for the picture.
    - Emotions only as faces and bodies show them.
- **Only what the game has given the player by the moment the picture appears.**
    - A character is named once the player knows them by sight. A first-person view or a figure the game keeps unknown stays unnamed ("a man in a mask", "a long clawed hand").
    - A place is named by what the player knows it as.
    - Nothing is said that the picture does not show: who is behind something, what happens next, what the designers called the scene.
- **Names as the game writes them in each language,** place words from the game's own labels, never the designers' internal names.
- **The length fits the time.**
    - A film's description is read while it plays: a clip of two seconds gets one short sentence, so that several in a row stay near their clips.
    - A long film is described in parts timed to its shots, each said as the film reaches it.
    - A film its caller shows only in part (the credits show each death film for a few seconds) is described only as far as it is shown.
- **The same structure in every language:** the English master is translated into every table at once, with the same facts; a comparison of the master against a translation catches a fact lost on the way.

## Clues

- **A clue's words are the game's.** Its menu shows a name, a text, details, and extra details behind a story flag. The mod never copies them when the clue is found, and never says them earlier than the game.
- **A clue's picture is described only for a story or lore detail its words leave out** (a tattoo on a photo that ties it to a later reveal, a friend crossed out on a board), never for looks: hair, clothes, background.
- **Before describing any interaction, check whether it is a clue's:** a clue screen shown by its Blueprint, a found-flag, a row of the clue table. Re-examining a clue already found says nothing unless the thing has changed since.

## How this mod keeps them

- **Three tables beside the mod's strings,** one file per language each, the English one the master:
    - `mod/lang/totems/<code>.ini`: `totem.<film>`, a totem's vision;
    - `mod/lang/films/<code>.ini`: `film.<film>`, and `film.<film>.<n>` for a film in parts;
    - `mod/lang/pictures/<code>.ini`: `look.<key>` for a thing an interaction shows, `look.<key>.<level>` per placing level, `look.<key>.<n>` for a book's spreads, `picture.<texture>` for a picture drawn in a menu or a camera's photo.

    `scripts\check-lang.ps1` holds every folder to its master.

- **Special values:** a value `=<key>` takes another entry's words (followed to the end), and a value `@<KEY>` is one of the game's own strings. A raw `@` or `=` value is never said: a description that leads nowhere gives no words.
- **A film's key** (`picture::FilmName`) is its file name without folder or extension, lower case.
    - The lookup (`picture::FilmKeys`, `VisionKeys`) tries the film's own key, then the key without a cut's suffix (clothes) and number.
    - A censored cut (`<film>_ja`, which `UBatesBinkMoviePlayer::GetMovieFilePath` plays while Extreme Content Censoring is on) is tried as `<key>_ja` under each of the film's keys first, then as the film it censors.
    - A film's parts and their start times are kept in `picture::PartsOf`.
- **The pictures of things are planned in `features/PictureRules`** (`kPlans`), each plan with a key, a class, a moment, an anim-graph state, whether it waits for the HUD's own words, and a level.
    - **Moments:** `Begins` as the interaction starts; `Reveals` when the item is turned to its face; `State` when the item's graph reaches a state (a map held up, a look standing); `Pages` per spread of a book.
    - `picture::PlansFor` matches an interaction by its core name, its name in the level's actor database, its class, the class's package, and the placing level. Many things have no core name.
    - `picture::DescribedKeys` and its test hold the English table to the plans both ways.
    - Every name a plan is keyed by is read in the level's data or logged at binding: "goes by core name ..., the level's name ...".
- **When a picture is said** (`picture::Due`): after the game's own prompt for the moment, and once the player's last-pressed key is let go (NVDA would cut it), within bounds. A plan with `afterHud` waits for the HUD's words that come with it (a clue's card, a document's page). Each picture is said once in a use, and again at the next use.
- **When a film is said:**
    - a totem's vision as it begins (`features/Totems`, the world and the totems menu);
    - a film of the dead after the character's name (`features/Credits`);
    - every other film by `features/Films`, which finds every `BinkMediaPlayer`. A film of the world is described, and read by F6, only while the thing its movie actor puts it on is drawn (`WasRecentlyRendered`, `picture::Sight`); a full-screen film has no such screen and counts as seen.
    - Each part once a showing, whichever player shows it. Nothing while paused, and a film resumed goes on where it stood.
    - A film looked at again in a new interaction is said again.
- **A long film whose narration leaves no room for words** (the events of the past) is described on a key: N or D-pad right says the part on screen. "Read description: key" is said 1.5 s after it begins, and "Static." over a part the player has not unlocked (`bIsShowingStatic`).
- **Photos and books.** A camera's photos are read by the texture it shows (`CurrentPictureTexture`), after the camera's prompt (`picture::PhotoPager`), the count "N of 5" at the detailed level. A book's spreads come by its graph's idle states (`picture::PageOf`), each cutting the last.
