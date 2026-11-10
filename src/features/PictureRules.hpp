#pragma once
// What the game shows without words and the mod describes, worked out without the game so that it
// can be tested: which description a film, a photo or a thing in the hand is kept under, and when a
// description is due. The descriptions themselves are the mod's tables (lang/totems, lang/films,
// lang/pictures), kept apart from the mod's other strings; a picture the game draws with words of
// its own is never described here (the game reads them out itself, translated, or they are left as
// the game prints them).

#include <optional>
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

    private:
        std::optional<size_t> m_said;
        double m_position = -1.0;
    };

    // The same for a totem's vision ("totem.totem_death_1"); the fifth hunger totem's three cuts
    // (totem_hunger_5_1 to _3, by what Jessica wears) fall back to "totem.totem_hunger_5", a censored
    // cut ("totem_death_5_ja") to the vision it censors.
    std::vector<std::wstring> VisionKeys(std::wstring_view name);

    // A picture by the texture the game draws it from ("picture.t_camera_sam_01_clr_up").
    std::wstring PictureKey(std::wstring_view texture);

    // When a thing the character handles shows its picture: as its interaction begins (a look at a
    // wall), as it is turned to its face (the game's reveal: Dr. Hill's card is held face down), as
    // the interaction's item comes to a state of its anim graph (the station's map held up), or
    // spread by spread as the item's graph comes to each (Sam's sketchbook: PageOf).
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
    };
    // The plan for an interaction by the name the game's scene scripts know it by (its core name,
    // "I_psychsession_1_Interaction"), or nothing when the mod describes nothing there.
    std::optional<LookPlan> PlanFor(std::wstring_view coreName);
    // The description's key for an interaction ("look.i_psychsession_1_interaction").
    std::wstring LookKey(std::wstring_view coreName);

    // The spread a book's anim graph shows in one of its states: its idles are named by the spread
    // ("page2_idle_right", "page4_idle_left1"; ABP_Act5_3_oldhotel_sams_sketchbook_template, whose
    // transitions take the stage's steps 0 to 3 to the spreads 1 to 4); nothing while a page turns or
    // the book is shut or put down. The description's key adds the spread ("look.i_sams_sketchbook.2").
    std::optional<int> PageOf(std::wstring_view state);
    std::wstring PageKey(std::wstring_view coreName, int page);

    // The prologue's camera (ABatesInteractionCamera) pages through its photos while it is held; a
    // photo is described as the camera is raised and each time the page turns to another, never
    // twice in a row for the same photo.
    class PhotoPager
    {
    public:
        // The camera's state at a poll: held or not, and the photo it shows (its index; -1 unknown).
        // Returns the index to describe now, if any.
        std::optional<int> Update(bool holding, int index);
        void Reset();

    private:
        bool m_holding = false;
        int m_said = -1;
    };
}
