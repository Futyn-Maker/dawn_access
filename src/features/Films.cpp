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
            bool inWorld = false;   // a film of the world's own (picture::FilmOwner::Films), asked where it is seen
            picture::Sight sight;   // whether its screen is drawn, and its words waiting for it
            picture::Screen screen = picture::Screen::Unknown; // as last polled, for the log
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

        // The thing a film of the world is drawn on, by the game's movie actor that plays it
        // (ABatesBinkMovieBase): the static mesh actor of the level it puts the film on
        // (ABatesBinkMovieStaticMesh::Object), asked as an actor, or its widget in the world
        // (ABatesBinkMovieProjected::InWorldWidgetComponent), asked as a component; a widget drawn
        // in screen space has no place in the scene and is not taken. Kept per film, whichever player
        // plays it (the cinema's screen and projector both play "smg_sting").
        struct ScreenOf
        {
            obj::ObjectKey key;
            std::wstring name; // for the log
        };
        struct FilmScreens
        {
            std::vector<ScreenOf> screens;
            bool found = false;       // any screen ever found for the film
            double lookedAt = -100.0; // when the movie actors were last looked through
        };
        std::map<std::wstring, FilmScreens> g_screens;

        void FindScreens(UObject* player, const std::wstring& film)
        {
            auto& known = g_screens[film];
            known.lookedAt = gamethread::NowSeconds();
            auto& screens = known.screens;
            for (UObject* movie : obj::FindAllLive(L"BatesBinkMovieBase"))
            {
                UObject* held = nullptr;
                if (!obj::ReadObject(movie, L"BinkMediaPlayer", held) || held != player) continue;
                UObject* screen = nullptr;
                if (obj::IsA(movie, L"BatesBinkMovieStaticMesh"))
                {
                    screen = obj::SoftObject(movie, L"Object");
                }
                else if (obj::IsA(movie, L"BatesBinkMovieProjected"))
                {
                    UObject* widget = nullptr;
                    int64_t space = -1; // EWidgetSpace: World 0, Screen 1
                    if (obj::ReadObject(movie, L"InWorldWidgetComponent", widget) && obj::IsLive(widget) && obj::ReadInt(widget, L"Space", space) && space == 0)
                        screen = widget;
                }
                if (!screen || !obj::IsLive(screen) || !obj::FindFunction(screen, L"WasRecentlyRendered"))
                {
                    log::Info(L"films: {} plays \"{}\" on nothing the mod can ask whether it is drawn", obj::ObjectName(movie), film);
                    continue;
                }
                const auto key = obj::KeyOf(screen);
                if (std::any_of(screens.begin(), screens.end(), [&](const ScreenOf& s) { return s.key == key; })) continue;
                screens.push_back(ScreenOf{key, obj::ObjectName(movie) + L" on " + obj::ObjectName(screen)});
                known.found = true;
                log::Info(L"films: \"{}\" is drawn by {} on {}; described while it is drawn", film, obj::ObjectName(movie), obj::ObjectName(screen));
            }
        }

        // Whether the engine drew the screen on the player's screen within the last fifth of a second
        // (AActor::WasRecentlyRendered, UPrimitiveComponent::WasRecentlyRendered).
        bool Drawn(const ScreenOf& s)
        {
            if (!obj::IsSame(s.key)) return false;
            UObject* screen = s.key.object;
            auto* fn = obj::FindFunction(screen, L"WasRecentlyRendered");
            if (!fn) return false;
            bool drawn = false;
            obj::Call(
                screen, fn,
                [&](void* params)
                {
                    for (auto* prop : fn->ForEachProperty())
                    {
                        if (prop && prop->GetName() == L"Tolerance") *static_cast<float*>(obj::ValuePtrAt(params, prop)) = 0.2f;
                    }
                },
                [&](void* params)
                {
                    for (auto* prop : fn->ForEachProperty())
                    {
                        if (prop && prop->GetName() == L"ReturnValue") drawn = *static_cast<const bool*>(obj::ValuePtrAt(params, prop));
                    }
                });
            return drawn;
        }

        // The film's screen as the player has it now (picture::ScreenOf): its screens gone with their
        // levels let go, and looked for again while none is left.
        picture::Screen ScreenNow(const std::wstring& film, UObject* player)
        {
            auto& known = g_screens[film];
            std::erase_if(known.screens, [](const ScreenOf& s) { return !obj::IsSame(s.key); });
            if (known.screens.empty() && known.found && player && gamethread::NowSeconds() - known.lookedAt >= picture::kScreensAgain)
                FindScreens(player, film);
            const bool drawn = std::any_of(known.screens.begin(), known.screens.end(), Drawn);
            return picture::ScreenOf(known.found, known.screens.size(), drawn);
        }

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
            p.onKey = false;
            p.keyDueAt = -1.0;
            p.inWorld = false;
            p.screen = picture::Screen::Unknown;
            p.sight = picture::Sight{};
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
            p.inWorld = !p.onKey;
            if (p.onKey)
            {
                log::Info(L"films: {} of {} plays \"{}\"; its {} parts are said on the key", obj::ObjectName(player), OwnerName(player), p.film, p.parts.size());
                return;
            }
            FindScreens(player, p.film);
            if (!p.parts.empty())
            {
                log::Info(L"films: {} of {} plays \"{}\"; it is described in {} parts as it plays", obj::ObjectName(player), OwnerName(player), p.film,
                          p.parts.size());
                return;
            }
            const std::wstring words = FilmWords(p.film);
            const bool fresh = !words.empty() && !g_showings[p.film].Said(-1);
            p.sight.Begin(fresh);
            log::Info(L"films: {} of {} plays \"{}\"; {}", obj::ObjectName(player), OwnerName(player), p.film,
                      words.empty() ? L"no words for it" : (fresh ? L"its words are said as it is seen" : L"its words were said in this showing already"));
        }

        // A film of the world as its screen is drawn or not: its waiting words said once it is seen, once
        // a showing (picture::Sight).
        void PollSight(Player& p)
        {
            // Nothing is said over the game's pause; what waits is said back in play, if then seen.
            if (gamestate::Paused()) return;
            const auto screen = ScreenNow(p.film, p.key.object);
            if (screen != p.screen)
            {
                if (screen != picture::Screen::Unknown)
                    log::Info(L"films: the screen of \"{}\" {}", p.film, screen == picture::Screen::Drawn ? L"is drawn" : L"is not drawn");
                p.screen = screen;
            }
            if (!p.sight.Update(screen)) return;
            if (!g_showings[p.film].Says(-1)) return;
            const std::wstring words = FilmWords(p.film);
            log::Info(L"films: \"{}\" is seen; its words are said", p.film);
            if (!words.empty()) Say(p, words);
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
            // The game's pause stops time for a showing: a projector the pause stops is not taken for a
            // film over and begun anew however long the pause lasts (the dialogue history, read line by
            // line, can hold it for minutes).
            if (gamestate::Paused())
            {
                for (auto& showing : g_showings)
                    showing.second.Playing(nowSeconds);
            }
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
                if (playing && p.inWorld && picture::OwnerOf(p.film) == picture::FilmOwner::Films) PollSight(p);
                // Nothing of a film is said while the game is paused, nor while its screen is not drawn; back
                // in play, or in sight, the part it stands in then is said, those it passed meanwhile are not
                // (picture::PartTracker).
                if (playing && !p.parts.empty() && !gamestate::Paused() && p.sight.Seen()) PollParts(p, player);
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
        g_screens.clear();
        // A map loaded anew shows its films anew.
        g_showings.clear();
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
        if (g_nowWords.empty() || !obj::IsSame(g_nowPlayer)) return;
        // A film of the world only while its screen is drawn (picture::Sight).
        for (const auto& p : g_players)
        {
            if (p.key == g_nowPlayer && !p.sight.Seen()) return;
        }
        out.push_back(g_nowWords);
    }

    void FilmsFeature::Help(std::vector<std::wstring>& out)
    {
        if (KeyFilm()) out.push_back(locale::Mod(L"help.film", hotkeys::SpokenKey(hotkeys::Command::NextTarget)));
    }
}
