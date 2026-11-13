#pragma once
// What the game shows without words and the mod describes, worked out without the game so that it
// can be tested: which description a film, a photo or a thing in the hand is kept under, and when a
// description is due. The descriptions themselves are the mod's tables (lang/totems, lang/films,
// lang/pictures), kept apart from the mod's other strings; a picture the game draws with words of
// its own is never described here (the game reads them out itself, translated, or they are left as
// the game prints them).

#include <array>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace da::picture
{
    // A film by the name its description is kept under: the file's name without the folder or the
    // extension, lower case as the tables' keys are: "Movies/Totem_Death_5_ja.bk2" is totem_death_5_ja.
    // The "_ja" cut is the censored one, which the game plays instead of the film when the player has
    // turned Extreme Content Censoring on (UBatesBinkMoviePlayer::GetMovieFilePath; always in the
    // Japanese edition, which the PC game never is): its words are its own where the table has them,
    // else the film's (FilmKeys, VisionKeys).
    std::wstring FilmName(std::wstring_view url);

    // Which part of the mod describes a film: the totems' visions their own feature (it knows the
    // totem in hand and the menu's page), the films of the dead the credits (they say whose death it
    // is first), every other film the films' watcher.
    enum class FilmOwner
    {
        Totems,
        Credits,
        Films,
    };
    FilmOwner OwnerOf(std::wstring_view name);

    // The keys a film's description is looked for under, the first found taken: its own name, then
    // its name without the suffix that marks a cut differing only in what the game varies by the
    // story and the description leaves out (Jessica's clothes, Mike's missing fingers), then without a
    // cut's number: the film of the dead "death_act8_5_jessica_leftbehind_jacket" is described as
    // "death_act8_5_jessica_leftbehind" unless the jacket's cut has words of its own. A censored cut
    // ("_ja") is looked for under each of the censored film's keys with "_ja" added first (so one
    // censored text serves every clothing cut), then as the film it censors.
    std::vector<std::wstring> FilmKeys(std::wstring_view name);

    // A long film is described in parts as it plays, each part said as the film comes to it: the
    // second each part begins at, by the film's name (its words kept under "film.<name>.<n>", n from 1).
    // Empty for a film described once, as it begins, under FilmKeys. A film of the dead the credits
    // describe at its start may have one later part, said only where the credits let the film run on to
    // it (the sisters' cliff film is shown 5 s by one ending's queue and 9 s by the other).
    std::vector<double> PartsOf(std::wstring_view name);
    std::wstring PartKey(std::wstring_view name, size_t part); // part from 0

    // Which part of a film to say as it plays: the part its position lies in, once, and none while the
    // film is not shown (the events of the past show static over a part not unlocked, and seek past
    // it: the part the film lands in is said, those it skipped are not). A film played again from an
    // earlier point says its parts again.
    class PartTracker
    {
    public:
        std::optional<size_t> Update(const std::vector<double>& parts, double position, bool shown);
        void Reset();
        // Where the film stood at the last update; below 0 before any.
        double Position() const
        {
            return m_position;
        }

    private:
        std::optional<size_t> m_said;
        double m_position = -1.0;
    };

    // One showing of a film, whichever player shows it: each part is said once (the whole film's words as
    // part -1). The cinema's screen and its projector play "smg_sting" together, the screen on through
    // the game's pause and the projector stopped with it, and each part was said by both, the first part
    // again at each return from the pause (session 71, 23:39:44 to 23:43:08). A showing is over once no
    // player has played the film for kShowingGap.
    constexpr double kShowingGap = 30.0;
    class Showing
    {
    public:
        // A player plays the film at `now`.
        void Playing(double now)
        {
            m_playedAt = now;
        }
        // Whether the part is to be said now: not said before in this showing.
        bool Says(int part)
        {
            return m_said.insert(part).second;
        }
        bool Said(int part) const
        {
            return m_said.count(part) != 0;
        }
        bool Over(double now) const
        {
            return m_playedAt >= 0.0 && now - m_playedAt > kShowingGap;
        }

    private:
        std::set<int> m_said;
        double m_playedAt = -1.0;
    };

    // Where the screen a film in the world plays on stands for the player: the thing the game's movie
    // actor puts it on (ABatesBinkMovieBase: a static mesh actor of the level, a widget in the world)
    // drawn on the player's screen now or not (the engine's WasRecentlyRendered), or no such screen
    // found (a full-screen film, a television a scene's blueprint feeds), which is taken as seen while
    // it plays.
    enum class Screen
    {
        Unknown,
        Drawn,
        Hidden,
    };

    // The film's screen from what the mod knows of its screens: none found for it ever (Unknown: a
    // full-screen film, a screen the game feeds by its own blueprint), one of them drawn, or none drawn,
    // those it had gone with their levels included: a film whose screen the world has let go is not
    // seen. Its screens are looked for again while it has none left (kScreensAgain), so that a level
    // streamed back, or another of the game's movie actors, shows it again (found by an independent
    // review: a screen that had gone left its film unseen for good, its words never said).
    constexpr double kScreensAgain = 2.0;
    Screen ScreenOf(bool everFound, size_t live, bool anyDrawn);

    // What of a film in the world is seen: a film is described only while its screen is drawn; one
    // whose screen is not drawn as it begins has its words wait until it is, within its showing. The
    // F6 readout reads it only while it is seen. Session 71: act4_3's CCTV monitor, shown by the act's
    // opening shot at 23:12:38, played on in the Stranger's room for the rest of the act, and its words
    // came with the map's at 23:19:59 on the read-screen key, Matt far off at the cable car station.
    class Sight
    {
    public:
        // The film begins; `words` when it has words of its own to say as it is seen.
        void Begin(bool words)
        {
            m_waiting = words;
        }
        // The screen as polled; true at the poll its waiting words are to be said.
        bool Update(Screen screen)
        {
            m_screen = screen;
            if (!m_waiting || screen == Screen::Hidden) return false;
            m_waiting = false;
            return true;
        }
        bool Seen() const
        {
            return m_screen != Screen::Hidden;
        }
        bool Waiting() const
        {
            return m_waiting;
        }

    private:
        Screen m_screen = Screen::Unknown;
        bool m_waiting = false;
    };

    // Whether a film a player plays again goes on from where it stood rather than begins anew: the same
    // film, past its first moment, at or past where it was. The game's pause stops the cinema's projector
    // with the game and starts it again, and the film was described from its first part at each return
    // (session 71, 23:40:15 and 23:42:14, the projector at 9.4 and 15.4 s).
    bool Resumes(const std::wstring& lastFilm, const std::wstring& film, double lastPosition, double position);

    // A long film whose parts are said on the player's key rather than as it plays: the events of the
    // past, whose narration runs through the whole film and leaves no room between its lines for the
    // words of a part (Andrey, 2026-11-10). The key is named once as the film begins; a press says the
    // part the film stands in then, alone.
    bool OnKey(std::wstring_view name);
    // The part a film's position lies in: none before its first part, or while the film is not shown.
    std::optional<size_t> PartAt(const std::vector<double>& parts, double position, bool shown);

    // The same for a totem's vision ("totem.totem_death_1"); the fifth hunger totem's three cuts
    // (totem_hunger_5_1 to _3, by what Jessica wears) fall back to "totem.totem_hunger_5", a censored
    // cut ("totem_death_5_ja") to the vision it censors.
    std::vector<std::wstring> VisionKeys(std::wstring_view name);

    // A picture by the texture the game draws it from ("picture.t_camera_sam_01_clr_up").
    std::wstring PictureKey(std::wstring_view texture);

    // When a thing the character handles shows its picture: as its interaction begins, as it is turned
    // to its face (the game's reveal: Dr. Hill's card is held face down), as the interaction's item
    // comes to a state of its anim graph (the station's map held up, a look at a wall once the look
    // stands), or spread by spread as the item's graph comes to each (Sam's sketchbook: PageOf).
    enum class Moment
    {
        Begins,
        Reveals,
        State,
        Pages,
    };
    struct LookPlan
    {
        Moment moment = Moment::Begins;
        std::wstring state; // the anim graph's state, for Moment::State
        std::wstring key;   // the name its description is kept under (LookKey, PageKey)
        // Shown with words of the HUD's own (a clue's card, a document's page): said after them (Due).
        bool afterHud = false;
    };
    // The plan for an interaction by any of the names the game gives it, or nothing when the mod
    // describes nothing there: its core name, its name in the level's actor database (the one its scene
    // scripts and activities call it by), or its class. Dr. Hill's card and Sam's sketchbook have no core
    // name (their CorePropertiesComponent sets none, in the level and the class alike), which the plans
    // had been keyed by, so neither was ever described (research/sessions/s67-play.log, 19:16:29); the
    // activity starts the card as "I_psychsession_1_Interaction" (act20_1's InteractionWithIdle stage).
    std::optional<LookPlan> PlanFor(const std::vector<std::wstring>& names);
    // Every plan for an interaction by any of its names, in the table's order: an interaction may show
    // one picture at one state and another at another, each said once a use. The lodge cellar's
    // dollhouse (act5_3, BP_Doll_house_open) is looked into through its windows at its first use
    // (LookinSequence), and opened with the key at its second (Open_Front).
    std::vector<LookPlan> PlansFor(const std::vector<std::wstring>& names);

    // The friend whose photo the psycho's board shows crossed out (act5_3, Scene02_kitchencorridorexplore
    // state 8): the first of the eight whose callforward PsychDislike<name> is set, in the script's order
    // (kDislikeOrder: Ash, Chris, Jessica, Emily, Josh, Matt, Mike, Sam), from Dr. Hill's sessions; -1
    // when none is read set (the script then falls to Ashley's, which the mod does not claim on a
    // reading it cannot tell from a missing one). In Andrey's second playthrough Emily's
    // (CallforwardPsychDislikeEmily, s72's saves).
    constexpr std::array<const wchar_t*, 8> kDislikeOrder = {L"Ash", L"Chris", L"Jessica", L"Emily", L"Josh", L"Matt", L"Mike", L"Sam"};
    constexpr std::array<const wchar_t*, 8> kDislikeNames = {L"Ashley", L"Chris", L"Jessica", L"Emily", L"Josh", L"Matt", L"Mike", L"Sam"};
    int CrossedOut(const std::array<bool, 8>& dislikes);
    // The board of the friends' photos (i_photos_of_friends_lookat_first at the first look,
    // i_photos_of_friends_lookat after): a clue whose crossed-out photo alone is said.
    bool IsFriendsBoard(std::wstring_view planKey);
    // The description's key for an interaction's plan ("look.i_psychsession_1_interaction").
    std::wstring LookKey(std::wstring_view planKey);

    // The words of a description: its text in the mod's table, or one of the game's own strings when
    // it reads "@<key>", or the words another description gives when it reads "=<key>" (one picture
    // shown in two places), followed through to the end: a description pointing at one that is a
    // game string is that game string. A game string not found, a description missing, a reference
    // round in a circle, give no words: a raw "@key" or "=key" is never said ("@SMG013_65750", the
    // board of the friends' photos looked at again, s73 11:47:52). `mod` answers a key of the mod's
    // table, empty when it has none; `game` a key of the game's string tables, empty when not found.
    std::wstring ResolveWords(std::wstring_view key, const std::function<std::wstring(std::wstring_view)>& mod,
                              const std::function<std::wstring(std::wstring_view)>& game);

    // The spread a book's anim graph shows in one of its states: its idles are named by the spread
    // ("page2_idle_right", "page4_idle_left1"; ABP_Act5_3_oldhotel_sams_sketchbook_template, whose
    // transitions take the stage's steps 0 to 3 to the spreads 1 to 4); nothing while a page turns or
    // the book is shut or put down. The description's key adds the spread ("look.i_sams_sketchbook.2").
    std::optional<int> PageOf(std::wstring_view state);
    std::wstring PageKey(std::wstring_view coreName, int page);

    // The prologue's camera (ABatesInteractionCamera) pages through its photos while it is held; a
    // photo is described as the camera is raised and each time the page turns to another, never
    // twice in a row for the same photo. The first waits for the game's own prompt (the stick or the
    // keys to page with, and the way back), said as the camera comes up: the photo then, after it
    // (Andrey, 2026-11-10), or by itself once kPromptWaitMost has gone without one. Every photo waits
    // for the player's key to be let go (Due), at most kKeyWaitMost from the page it shows.
    constexpr double kPromptSettle = 0.25;   // after the prompt, for the action bar's line said with it
    constexpr double kPromptWaitMost = 2.0;  // from the camera raised
    class PhotoPager
    {
    public:
        // The camera's state at a poll: held or not, and the photo it shows (its index; -1 unknown);
        // `promptAt` when the game's prompt was last said (-1 never); `keyHeld` the key the player last
        // pressed still down (keyhook::KeyRepeating). Returns the index to describe now.
        std::optional<int> Update(bool holding, int index, double now, double promptAt, bool keyHeld = false);
        void Reset();
        // The photo described last while the camera is held (-1 none).
        int Said() const
        {
            return m_said;
        }

    private:
        bool m_holding = false;
        double m_heldAt = 0.0;
        int m_said = -1;
        int m_index = -1;      // the photo shown, said or not
        double m_indexAt = 0.0; // since when
    };

    // A picture's words, once its moment has come (a card turned, a state come to, a spread opened),
    // wait for two things. The game's prompts drawn with it, said first (Andrey, 2026-11-11: "first
    // the prompt, then the image", as the camera's photos): a prompt said since the moment and
    // kPromptSettle after it, or kLookPromptWaitMost without one (a prompt said before the moment
    // needs no waiting for: the map's were said as it was taken down); a spread turned to after the
    // first waits for no prompt, its step's prompt said as its page turned. And the key the player last
    // pressed, let go: Windows repeats a held key, and a screen reader given each repeat may stop what
    // it says at every one (NVDA's speech interrupt for typed characters, on by default). Dr. Hill's
    // card came face up as D still turned it, and its words, begun at once, were cut; "Назад:
    // Backspace", queued 0.2 s behind them, was heard (research/sessions/s68-play.log, 01:03:36.254
    // to .452). The key is waited for at most kKeyWaitMost: a key held on longer is not let go for it.
    constexpr double kLookPromptWaitMost = 1.0;
    constexpr double kKeyWaitMost = 4.0;
    // A picture shown with words of the HUD's own (LookPlan::afterHud) waits for them, at most kHudWaitMost:
    // a clue's card came 1.2 s into the look at the old hotel's dead pig, its picture before it by the
    // prompt's rule (research/sessions/s58-whole.log, 11:44:24.879 to 26.076), and the locker's poster
    // of Hannah has its transcript read 1.4 s after the look stands (s72, 11:29:14.43 to 15.79); the
    // words first, then the picture, as Andrey asked of Hannah's pinboard (s71).
    constexpr double kHudWaitMost = 3.0;
    class Due
    {
    public:
        // The moment has come at `now`; `awaitPrompt`: a prompt may come with it; `afterHud`: words of
        // the HUD's own come with it, said first.
        void Set(double now, bool awaitPrompt, bool afterHud = false);
        // Whether the words are to be said at this poll: once. `promptAt` when the game's prompt was
        // last said (-1 never), `keyHeld` the player's key still down, `hudSaidAt` when the HUD last
        // said words of its own (-1 never).
        bool Ready(double now, double promptAt, bool keyHeld, double hudSaidAt = -1.0);
        bool Pending() const
        {
            return m_pending;
        }
        // Since when it waits.
        double Since() const
        {
            return m_at;
        }
        // Whether the words wait for the player's key at this poll (for the log).
        bool WaitsForKey(double now, bool keyHeld) const;
        void Reset();

    private:
        bool m_pending = false;
        bool m_awaitPrompt = false;
        bool m_afterHud = false;
        double m_at = 0.0;
    };
}
