#pragma once
// The films the game plays in the world or over it (a television, a projector's film on a wall, a
// monitor, the epilogue's last film), other than the totems' visions and the films of the dead: a film
// the mod has words for is described as it begins (lang/films). The game plays every film through a
// Bink media player (UBinkMediaPlayer): one for each television, projector and monitor of a level, one
// for the films over the screen (UBatesBinkMoviePlayer), the totems' and the credits' own. Each is
// watched for the film it opens and whether it plays; the totems' visions and the films of the dead
// are left to their features (picture::OwnerOf), which know whose they are. The events of the past,
// narrated throughout, are described on the player's key instead (picture::OnKey): N or the D-pad's
// right says the part the film stands in.
#include "features/Feature.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace da::features
{
    class FilmsFeature : public Feature
    {
    public:
        const wchar_t* Name() const override
        {
            return L"Films";
        }
        void Install() override;
        void Describe(std::vector<std::wstring>& out) override;
        void Help(std::vector<std::wstring>& out) override;
    };

    // A map was loaded: the players of its levels are looked for anew.
    void ResetFilms();

    // The words for a film by its name (picture::FilmName), empty when the mod has none.
    std::wstring FilmWords(const std::wstring& name);

    // A description from the mod's tables by its key; one that names a string of the game's (@key) is
    // that string, in the game's language.
    std::wstring Words(std::wstring_view key);
    // The player begins another look at the world (an interaction): the films playing on are said again
    // as they are seen (picture::Showing::Again).
    void FilmsLookedAtAgain();
}
