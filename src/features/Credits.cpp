#include "features/Credits.hpp"

#include "features/CreditsRules.hpp"
#include "features/Films.hpp"
#include "features/PictureRules.hpp"

#include "core/GameThread.hpp"
#include "core/Log.hpp"
#include "core/ObjectUtil.hpp"
#include "core/Strings.hpp"
#include "core/Symbols.hpp"
#include "hotkeys/Hotkeys.hpp"
#include "locale/GameText.hpp"
#include "locale/Locale.hpp"
#include "speech/Speech.hpp"
#include "watch/GameState.hpp"
#include "watch/Watchers.hpp"

#include <map>
#include <string>
#include <vector>

namespace da::features
{
    using RC::Unreal::UObject;

    namespace
    {
        // The manager's three widgets, as the screen's watcher finds them; each is made once for the
        // epilogue and shown and hidden as its turn comes.
        obj::ObjectKey g_rollWidget;
        obj::ObjectKey g_overlayWidget;
        obj::ObjectKey g_videoWidget;
        // The manager itself, found once a film is shown (it knows which film it runs).
        obj::ObjectKey g_manager;
        double g_managerSoughtAt = -100.0;

        UObject* Live(const obj::ObjectKey& key)
        {
            return obj::IsSame(key) ? key.object : nullptr;
        }

        // ---- the roll ----

        credits::Roll g_lines;                        // the roll's lines, each once in its order
        credits::Taker g_taker;                       // which lines of the container are kept already
        credits::LineIds g_lineIds;                   // which line each pooled line widget shows now
        std::map<obj::ObjectKey, uint64_t> g_widgets; // a number for each line widget of the roll
        uint64_t g_nextWidget = 1;
        bool g_rollShown = false;
        double g_rollHiddenAt = -1.0;
        obj::ObjectKey g_rollKey; // the widget the roll kept so far belongs to
        bool g_keySaid = false;   // the key that reads the roll was said for this roll
        // A roll hidden this long and shown again is a new roll; hidden a moment, the same.
        constexpr double kRollGap = 3.0;

        UObject* RollUp()
        {
            UObject* roll = Live(g_rollWidget);
            return roll && obj::IsWidgetShown(roll) ? roll : nullptr;
        }

        // The roll is up in play, no menu over it: the key that reads it is the mod's.
        bool CreditsUp()
        {
            return RollUp() != nullptr && gamestate::InPlay() && !watch::CurrentScreen();
        }

        uint64_t WidgetNumber(UObject* line)
        {
            if (!line) return 0;
            const auto key = obj::KeyOf(line);
            const auto found = g_widgets.find(key);
            if (found != g_widgets.end()) return found->second;
            g_widgets.emplace(key, g_nextWidget);
            return g_nextWidget++;
        }

        // How far a line widget has rolled (CurrentOffset, which its setting up puts back to 0); -1
        // where it cannot be read.
        double OffsetOf(UObject* line)
        {
            const auto* offset = static_cast<const float*>(sym::MemberOf(line, L"UBatesCreditLineWidget.CurrentOffset"));
            return offset ? static_cast<double>(*offset) : -1.0;
        }

        // A line of the roll as words: its entries' texts side by side (credits::LineOf).
        std::wstring WordsOf(UObject* line)
        {
            std::vector<UObject*> entries;
            if (!obj::ReadObjectArray(line, L"SpawnedWidgets", entries)) return {};
            std::vector<std::wstring> texts;
            for (UObject* entry : entries)
            {
                UObject* text = nullptr;
                if (!obj::IsLive(entry) || !obj::ReadObject(entry, L"Text_Name", text) || !obj::IsLive(text)) continue;
                texts.push_back(obj::TextOf(text));
            }
            return credits::LineOf(texts);
        }

        // The line widgets on the roll's container, in its order: the order the roll drew them in.
        std::vector<UObject*> LinesOn(UObject* roll)
        {
            std::vector<UObject*> out;
            UObject* container = nullptr;
            if (!obj::ReadObject(roll, L"CreditContainer", container) || !obj::IsLive(container)) return out;
            for (UObject* child : obj::PanelChildren(container))
            {
                if (obj::IsLive(child) && obj::IsA(child, L"BatesCreditLineWidget")) out.push_back(child);
            }
            return out;
        }

        void PollRoll(double now)
        {
            UObject* roll = RollUp();
            if (!roll)
            {
                if (g_rollShown)
                {
                    g_rollShown = false;
                    g_rollHiddenAt = now;
                    log::Info(L"credits: the roll is gone, {} lines kept", g_lines.Count());
                }
                return;
            }
            if (!g_rollShown)
            {
                g_rollShown = true;
                const bool anew = !(obj::KeyOf(roll) == g_rollKey) || g_rollHiddenAt < 0.0 || now - g_rollHiddenAt >= kRollGap;
                if (anew)
                {
                    g_rollKey = obj::KeyOf(roll);
                    g_lines.Clear();
                    g_taker.Clear();
                    g_lineIds.Clear();
                    g_widgets.clear();
                    g_keySaid = false;
                    log::Info(L"credits: the roll begins ({}): kept, read on the player's key", obj::ObjectName(roll));
                }
            }
            // Each widget's line: the roll's line widgets are pooled and set up again further on
            // (credits::LineIds).
            const auto lines = LinesOn(roll);
            std::vector<credits::LineWidget> widgets;
            widgets.reserve(lines.size());
            for (UObject* line : lines)
                widgets.push_back(credits::LineWidget{WidgetNumber(line), OffsetOf(line)});
            UObject* newest = nullptr;
            if (!obj::ReadObject(roll, L"ActiveCreditLine", newest) || !obj::IsLive(newest)) newest = nullptr;
            const auto ids = g_lineIds.Update(widgets, WidgetNumber(newest));
            std::vector<credits::Drawn> drawn;
            drawn.reserve(lines.size());
            for (size_t i = 0; i < lines.size(); ++i)
            {
                // A line kept already needs no reading.
                drawn.push_back(credits::Drawn{ids[i], g_taker.Taken(ids[i]) ? std::wstring() : WordsOf(lines[i])});
            }
            for (const auto& line : g_taker.Take(drawn, now))
            {
                g_lines.Add(line);
                log::Info(L"credits: line {} \"{}\"", g_lines.Count(), line);
            }
            if (!g_keySaid && g_lines.Count() > 0)
            {
                g_keySaid = true;
                const auto words = locale::Mod(L"credits.start", hotkeys::SpokenKey(hotkeys::Command::NextTarget));
                log::Info(L"credits: the key to read them is said: \"{}\"", words);
                speech::Announce(words);
            }
        }

        bool OnRead()
        {
            if (!CreditsUp()) return false;
            const auto unread = g_lines.Unread();
            if (unread.empty())
            {
                log::Info(L"credits: read on the key: no new lines");
                speech::Now(locale::Mod(L"credits.none"));
                return true;
            }
            log::Info(L"credits: read on the key: {} lines, {} kept in all", unread.size(), g_lines.Count());
            speech::Now(str::JoinSentences(unread));
            return true;
        }

        // ---- the interviews' overlay ----

        credits::Overlay g_overlay;

        std::wstring TextIn(UObject* widget, const wchar_t* member)
        {
            UObject* text = nullptr;
            if (!obj::ReadObject(widget, member, text) || !obj::IsLive(text)) return {};
            return obj::TextOf(text);
        }

        void PollOverlay(double now)
        {
            UObject* overlay = Live(g_overlayWidget);
            const bool shown = overlay && obj::IsWidgetShown(overlay);
            const auto room = shown ? TextIn(overlay, L"Text_Room") : std::wstring();
            const auto time = shown ? TextIn(overlay, L"Text_Time") : std::wstring();
            const auto words = g_overlay.Update(shown, room, time, now);
            if (words.empty()) return;
            log::Info(L"credits: the interview's overlay says \"{}\" (tape {})", words, str::CollapseWhitespace(TextIn(overlay, L"Text_Id")));
            speech::Announce(words);
        }

        // ---- the films of the dead ----

        obj::ObjectKey g_filmSaid; // the film whose death was said
        std::wstring g_filmName;   // what was said for it: the name, and what the film shows

        UObject* Manager(double now)
        {
            if (UObject* manager = Live(g_manager)) return manager;
            if (now - g_managerSoughtAt < 2.0) return nullptr;
            g_managerSoughtAt = now;
            UObject* manager = obj::FindFirstLive(L"BatesCreditsManager");
            g_manager = obj::KeyOf(manager);
            return manager;
        }

        std::wstring StringMember(UObject* object, const wchar_t* member)
        {
            const void* raw = sym::MemberOf(object, member);
            if (!raw) return {};
            struct RawString
            {
                const wchar_t* data;
                int32_t num;
                int32_t max;
            };
            const auto* text = static_cast<const RawString*>(raw);
            if (!text->data || text->num <= 1 || text->num > 1024) return {};
            return std::wstring(text->data, static_cast<size_t>(text->num - 1));
        }

        void PollVideo(double now)
        {
            UObject* video = Live(g_videoWidget);
            if (!video || !obj::IsWidgetShown(video)) return;
            UObject* manager = Manager(now);
            UObject* film = nullptr;
            if (!manager || !obj::ReadObject(manager, L"ActiveCreditQueueObject", film) || !obj::IsLive(film) ||
                !obj::IsA(film, L"BatesCreditMovieQueueObject"))
                return;
            const auto key = obj::KeyOf(film);
            if (key == g_filmSaid) return;
            g_filmSaid = key;
            // Whose death it shows, by the name the scene's script gave it (a key of the game's tables);
            // the time of death it also gave is drawn nowhere, and left unsaid.
            const auto nameKey = StringMember(film, L"UBatesCreditMovieQueueObject.CharacterName");
            const auto deathKey = StringMember(film, L"UBatesCreditMovieQueueObject.TimeOfDeath");
            const auto identifier = StringMember(film, L"UBatesCreditQueueObjectBase.Identifier");
            const std::wstring name = nameKey.empty() ? std::wstring() : str::CollapseWhitespace(gametext::Resolve(nameKey));
            // What the film shows, after whose death it is (lang/films, by the film's name).
            const std::wstring words = FilmWords(picture::FilmName(identifier));
            log::Info(L"credits: the film {} shows the death of \"{}\" ({}; the time of death \"{}\" is drawn nowhere); {}", identifier, name,
                      nameKey.empty() ? L"no name given" : nameKey, deathKey.empty() ? std::wstring() : gametext::Resolve(deathKey),
                      words.empty() ? L"no words for it" : L"its words are said");
            g_filmName = str::JoinSentences({name, words});
            if (!g_filmName.empty()) speech::Announce(g_filmName);
        }

        void PollImpl()
        {
            if (gamethread::FrameCount() % 3 != 0) return;
            const double now = gamethread::NowSeconds();
            PollVideo(now);
            PollOverlay(now);
            PollRoll(now);
        }

        void Poll(float)
        {
            obj::SafeInvokeLogged(L"credits.PollImpl", [](void*) { PollImpl(); }, nullptr);
        }

        void Watch(const wchar_t* cls, obj::ObjectKey& key)
        {
            watch::OnWidgetClass(cls,
                                 [&key, cls](UObject* widget, bool appeared)
                                 {
                                     if (!appeared || !widget) return;
                                     if (!(obj::KeyOf(widget) == key)) log::Info(L"credits: {} is {}", cls, obj::ObjectName(widget));
                                     key = obj::KeyOf(widget);
                                 });
        }
    }

    void CreditsFeature::Install()
    {
        Watch(L"BatesCreditWidget", g_rollWidget);
        Watch(L"BatesCreditCCTVWidget", g_overlayWidget);
        Watch(L"BatesCreditVideoWidget", g_videoWidget);
        hotkeys::AddContext(&CreditsUp);
        hotkeys::OnCommand(hotkeys::Command::NextTarget, &OnRead);
        gamethread::AddPoller(L"credits", &Poll);
    }

    void CreditsFeature::Describe(std::vector<std::wstring>& out)
    {
        UObject* video = Live(g_videoWidget);
        if (video && obj::IsWidgetShown(video) && !g_filmName.empty()) out.push_back(g_filmName);
        UObject* overlay = Live(g_overlayWidget);
        if (overlay && obj::IsWidgetShown(overlay))
        {
            const auto words = credits::OverlayWords(TextIn(overlay, L"Text_Room"), TextIn(overlay, L"Text_Time"));
            out.push_back(str::JoinSentences({words, str::CollapseWhitespace(TextIn(overlay, L"Text_Id"))}));
        }
        if (UObject* roll = RollUp())
        {
            std::vector<std::wstring> lines;
            for (UObject* line : LinesOn(roll))
            {
                const auto words = WordsOf(line);
                if (!words.empty()) lines.push_back(words);
            }
            if (!lines.empty()) out.push_back(str::JoinSentences(lines));
        }
    }

    void CreditsFeature::Help(std::vector<std::wstring>& out)
    {
        if (!CreditsUp()) return;
        out.push_back(locale::Mod(L"help.credits", hotkeys::SpokenKey(hotkeys::Command::NextTarget)));
    }
}
