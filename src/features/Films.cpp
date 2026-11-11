#include "features/Films.hpp"

#include "features/PictureRules.hpp"

#include "core/GameThread.hpp"
#include "core/Log.hpp"
#include "core/ObjectUtil.hpp"
#include "core/Symbols.hpp"
#include "hooks/HookDispatcher.hpp"
#include "hotkeys/Hotkeys.hpp"
#include "locale/GameText.hpp"
#include "locale/Locale.hpp"
#include "speech/Speech.hpp"
#include "watch/GameState.hpp"

#include <algorithm>
#include <cstdint>
#include <format>
#include <map>
#include <string>
#include <vector>

namespace da::features
{
    using RC::Unreal::FProperty;
    using RC::Unreal::UObject;

    namespace
    {
        struct Player
        {
            obj::ObjectKey key;
            std::wstring film;    // the film it last had open (picture::FilmName)
            bool playing = false; // as last polled
            std::vector<double> parts; // a long film's parts (picture::PartsOf), for one described as it plays
            picture::PartTracker said;
            bool onKey = false;     // its parts are said on the player's key (picture::OnKey)
            double keyDueAt = -1.0; // when the key is named, once the page over the film has been read
        };

        // Where the film stands, in seconds (UBinkMediaPlayer::GetTime, a timespan of 100 ns ticks).
        double Position(UObject* player)
        {
            double seconds = -1.0;
            obj::CallReturn(player, L"GetTime",
                            [&](void* params, FProperty* ret)
                            {
                                if (!ret) return;
                                const auto* ticks = static_cast<const int64_t*>(obj::ValuePtrAt(params, ret));
                                if (ticks) seconds = static_cast<double>(*ticks) / 1e7;
                            });
            return seconds;
        }

        // The events of the past show static over a part not unlocked while they seek past it.
        bool Shown(UObject* player)
        {
            if (!obj::IsA(player, L"BatesEventsOfThePastMoviePlayer")) return true;
            const auto* staticShown = static_cast<const bool*>(sym::MemberOf(player, L"UBatesEventsOfThePastMoviePlayer.bIsShowingStatic"));
            return !staticShown || !*staticShown;
        }

        std::vector<Player> g_players;
        bool g_scanned = false;
        std::map<std::wstring, picture::Showing> g_showings; // each film's showing, whichever player shows it

        // The film described last and still playing, for the screen's readout.
        obj::ObjectKey g_nowPlayer;
        std::wstring g_nowWords;

        // The key is named a moment after the film begins: the page the film plays on is read as it
        // arrives, 1.2 s in (WBP_Totem_EOTP_Overlay, research/sessions/s66-play.log, 15:56:53.945 to
        // 15:56:55.157), and its reading cut the film's first words then.
        constexpr double kKeyAfter = 1.5;

        void Track(UObject* player)
        {
            if (!player || !obj::IsLive(player)) return;
            for (const auto& p : g_players)
            {
                if (p.key.object == player && obj::IsSame(p.key)) return;
            }
            g_players.push_back(Player{obj::KeyOf(player)});
        }

        // The players made before the mod watched for new ones: the game's own over the screen, and
        // a level's when a map was loaded.
        void Scan()
        {
            g_scanned = true;
            const auto all = obj::FindAllLive(L"BinkMediaPlayer");
            for (UObject* player : all) Track(player);
            log::Info(L"films: {} film player(s) found", all.size());
        }

        std::wstring OwnerName(UObject* player)
        {
            UObject* outer = obj::Outer(player);
            return outer ? obj::ObjectName(outer) : std::wstring(L"<none>");
        }

        void Say(const Player& p, const std::wstring& words)
        {
            g_nowPlayer = p.key;
            g_nowWords = words;
            speech::Announce(words);
        }

        void Began(Player& p, UObject* player)
        {
            p.parts.clear();
            p.said.Reset();
            const auto owner = picture::OwnerOf(p.film);
            if (owner != picture::FilmOwner::Films)
            {
                // The credits say a film of the dead as it begins; a later part of it is said here, if the
                // credits let it play on that far (picture::PartsOf).
                if (owner == picture::FilmOwner::Credits) p.parts = picture::PartsOf(p.film);
                log::Info(L"films: {} of {} plays \"{}\"; {} describe it{}", obj::ObjectName(player), OwnerName(player), p.film,
                          owner == picture::FilmOwner::Totems ? L"the totems" : L"the credits",
                          p.parts.empty() ? std::wstring() : std::format(L", its later part here at {:.1f} s", p.parts.front()));
                return;
            }
            p.parts = picture::PartsOf(p.film);
            p.onKey = !p.parts.empty() && picture::OnKey(p.film);
            p.keyDueAt = p.onKey ? gamethread::NowSeconds() + kKeyAfter : -1.0;
            if (p.onKey)
            {
                log::Info(L"films: {} of {} plays \"{}\"; its {} parts are said on the key", obj::ObjectName(player), OwnerName(player), p.film, p.parts.size());
                return;
            }
            if (!p.parts.empty())
            {
                log::Info(L"films: {} of {} plays \"{}\"; it is described in {} parts as it plays", obj::ObjectName(player), OwnerName(player), p.film,
                          p.parts.size());
                return;
            }
            const std::wstring words = FilmWords(p.film);
            const bool fresh = g_showings[p.film].Says(-1);
            log::Info(L"films: {} of {} plays \"{}\"; {}", obj::ObjectName(player), OwnerName(player), p.film,
                      words.empty() ? L"no words for it" : (fresh ? L"its words are said" : L"its words were said in this showing already"));
            if (!words.empty() && fresh) Say(p, words);
        }

        // A long film's part, as the film comes to it.
        void PollParts(Player& p, UObject* player)
        {
            const double position = Position(player);
            if (position < 0.0) return;
            const auto part = p.said.Update(p.parts, position, Shown(player));
            if (!part) return;
            const auto key = picture::PartKey(p.film, *part);
            const bool described = locale::Has(key);
            // A film said on the key only notes where its parts begin, for the log.
            if (p.onKey)
            {
                log::Info(L"films: \"{}\" comes to its part {} at {:.1f} s; said on the key", p.film, *part + 1, position);
                return;
            }
            if (!g_showings[p.film].Says(static_cast<int>(*part)))
            {
                log::Info(L"films: \"{}\" comes to its part {} at {:.1f} s on {}; said already in this showing", p.film, *part + 1, position,
                          obj::ObjectName(player));
                return;
            }
            log::Info(L"films: \"{}\" comes to its part {} at {:.1f} s; {}", p.film, *part + 1, position, described ? L"its words are said" : L"no words for it");
            if (described) Say(p, Words(key));
        }

        void PollImpl()
        {
            if (gamethread::FrameCount() % 6 != 0) return;
            if (!g_scanned) Scan();
            std::erase_if(g_players, [](const Player& p) { return !obj::IsSame(p.key); });
            const double nowSeconds = gamethread::NowSeconds();
            std::erase_if(g_showings, [&](const auto& showing) { return showing.second.Over(nowSeconds); });
            for (auto& p : g_players)
            {
                UObject* player = p.key.object;
                std::wstring url;
                obj::ReadString(player, L"URL", url);
                const std::wstring film = picture::FilmName(url);
                const bool playing = !film.empty() && obj::CallForBool(player, L"IsPlaying");
                bool began = playing && (!p.playing || film != p.film);
                // Played again where it stood, as the game's pause stops a player and starts it (the cinema's
                // projector): it goes on, its parts not said again (picture::Resumes).
                if (began && p.playing == false && !p.parts.empty())
                {
                    const double position = Position(player);
                    if (picture::Resumes(p.film, film, p.said.Position(), position))
                    {
                        log::Info(L"films: {} goes on with \"{}\" at {:.1f} s", obj::ObjectName(player), film, position);
                        began = false;
                    }
                }
                p.playing = playing;
                p.film = film;
                if (playing) g_showings[film].Playing(gamethread::NowSeconds());
                if (began) Began(p, player);
                // Nothing of a film is said while the game is paused; back in play, the part it stands in
                // then is said, those it passed meanwhile are not (picture::PartTracker).
                if (playing && !p.parts.empty() && !gamestate::Paused()) PollParts(p, player);
                if (playing && p.onKey && p.keyDueAt >= 0.0 && gamethread::NowSeconds() >= p.keyDueAt)
                {
                    p.keyDueAt = -1.0;
                    const auto words = locale::Mod(L"film.start", hotkeys::SpokenKey(hotkeys::Command::NextTarget));
                    log::Info(L"films: the key to read \"{}\" is said: \"{}\"", p.film, words);
                    speech::Announce(words);
                }
                if (!playing && p.key == g_nowPlayer) g_nowWords.clear();
            }
        }

        void Poll(float)
        {
            obj::SafeInvokeLogged(L"films.PollImpl", [](void*) { PollImpl(); }, nullptr);
        }

        // The film playing now whose parts are said on the key.
        Player* KeyFilm()
        {
            for (auto& p : g_players)
            {
                if (p.onKey && p.playing && obj::IsSame(p.key)) return &p;
            }
            return nullptr;
        }

        bool KeyFilmUp()
        {
            return KeyFilm() != nullptr;
        }

        // The part the film stands in (picture::PartAt); over static, the word for it.
        std::wstring PartWords(Player& p, double& position, bool& shown)
        {
            UObject* player = p.key.object;
            position = Position(player);
            shown = Shown(player);
            const auto part = picture::PartAt(p.parts, position, shown);
            if (!part) return shown ? std::wstring() : locale::Mod(L"film.static");
            const auto key = picture::PartKey(p.film, *part);
            return locale::Has(key) ? Words(key) : std::wstring();
        }

        bool OnRead()
        {
            Player* p = KeyFilm();
            if (!p) return false;
            double position = 0.0;
            bool shown = true;
            const auto words = PartWords(*p, position, shown);
            log::Info(L"films: \"{}\" read on the key at {:.1f} s{}", p->film, position, shown ? L"" : L", over static");
            if (!words.empty()) speech::Now(words);
            return true;
        }
    }

    std::wstring Words(std::wstring_view key)
    {
        std::wstring words = locale::Mod(key);
        // A description may be one of the game's own strings, said in the game's language, or the
        // words of another description of the table (=key), for one picture shown in two places.
        if (words.starts_with(L"@")) return gametext::Resolve(words.substr(1));
        if (words.starts_with(L"=") && words.size() > 1 && words.substr(1) != key) return locale::Mod(words.substr(1));
        return words;
    }

    std::wstring FilmWords(const std::wstring& name)
    {
        for (const auto& key : picture::FilmKeys(name))
        {
            if (locale::Has(key)) return Words(key);
        }
        return {};
    }

    void ResetFilms()
    {
        g_players.clear();
        g_scanned = false;
        g_nowPlayer = {};
        g_nowWords.clear();
    }

    void FilmsFeature::Install()
    {
        // Every player made from now on, the Events of the Past's own kind included.
        hooks::OnConstructed(L"BinkMediaPlayer", false, [](UObject* player) { Track(player); });
        hooks::OnConstructed(L"BatesEventsOfThePastMoviePlayer", false, [](UObject* player) { Track(player); });
        // A film said on the key takes N and the D-pad's right while it plays, as the credits do.
        hotkeys::AddContext(&KeyFilmUp);
        hotkeys::OnCommand(hotkeys::Command::NextTarget, &OnRead);
        gamethread::AddPoller(L"films", &Poll);
    }

    void FilmsFeature::Describe(std::vector<std::wstring>& out)
    {
        if (Player* p = KeyFilm())
        {
            double position = 0.0;
            bool shown = true;
            const auto words = PartWords(*p, position, shown);
            if (!words.empty()) out.push_back(words);
            return;
        }
        if (!g_nowWords.empty() && obj::IsSame(g_nowPlayer)) out.push_back(g_nowWords);
    }

    void FilmsFeature::Help(std::vector<std::wstring>& out)
    {
        if (KeyFilm()) out.push_back(locale::Mod(L"help.film", hotkeys::SpokenKey(hotkeys::Command::NextTarget)));
    }
}
