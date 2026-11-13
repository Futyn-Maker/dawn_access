#include "features/Hud.hpp"

#include "features/Exploration.hpp"
#include "features/TextRules.hpp"
#include "features/WorldRead.hpp"

#include "core/GameThread.hpp"
#include "core/Log.hpp"
#include "core/ObjectUtil.hpp"
#include "core/ParamReader.hpp"
#include "core/Strings.hpp"
#include "core/Symbols.hpp"
#include "hooks/HookDispatcher.hpp"
#include "input/InputNames.hpp"
#include "locale/Locale.hpp"
#include "speech/Speech.hpp"
#include "ui/Widgets.hpp"
#include "watch/GameState.hpp"
#include "watch/Watchers.hpp"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

namespace da::features
{
    using RC::Unreal::FFrame;
    using RC::Unreal::UObject;

    namespace
    {
        // The elements the player acts on, read by their own features, and the pages of the
        // menus: never read here, whatever text they carry.
        const wchar_t* const kOwnedElsewhere[] = {L"BatesSubtitlesWidget", L"BatesQTEWidget", L"BatesDirectionalChoiceWidget", L"BatesDontMoveWidget",
                                                  L"BatesDontMoveTutorialWidget", L"BatesInteractionWidget", L"BatesGlintWidget", L"BMUIPage",
                                                  L"BMUIActionBarWidget", L"BatesPersistentCentreDotWidget", L"BatesShootingStageTargetWidget",
                                                  L"BatesShootingStageReticuleWidget", L"BatesTTSIconWidget", L"BMUIHUDContainer", L"BatesHUDContainer",
                                                  // The epilogue's credits, its interviews' overlay and
                                                  // its videos of the dead (features/Credits).
                                                  L"BatesCreditWidget", L"BatesCreditLineWidget", L"BatesCreditEntryWidget", L"BatesCreditOverlayWidget"};

        bool OwnedElsewhere(UObject* widget)
        {
            for (const wchar_t* cls : kOwnedElsewhere)
            {
                if (obj::IsA(widget, cls)) return true;
            }
            return false;
        }

        // An element of the HUD and its lines as the rules weigh them (lines::Element).
        struct Tracked
        {
            UObject* instance = nullptr;
            lines::Element lines;
            lines::Card card;        // for an update card, which waits for its body (Text_Body)
            std::wstring cardTitle;  // what the card drew while its body was still to come
            lines::TitleFill fill;   // for a title card, which fades its words in one after another
            lines::ClockTicks clock; // a clock it shows, said as its hours and minutes change
        };

        std::vector<Tracked> g_tracked;
        std::set<std::wstring> g_classesSeen; // for the log: each kind of element once
        lines::Echo g_echo;                   // the same words through two elements at once
        lines::Reading g_reading;             // a document read page by page while she is held by it
        std::wstring g_pageSaid;              // the page of it said last, which a turned page cuts short

        // The lines an element shows: the texts it draws now (not the ones still fading in,
        // nor the ones faded out), a row of pieces side by side as one line (the camcorder's
        // minutes, colon and seconds), then the prompts of its key caps, one line each.
        std::vector<std::wstring> Lines(UObject* widget)
        {
            // A screen whose texts are its furniture (the selfie camera's "3" by a stopwatch) is read
            // by its prompts alone (lines::FurnitureOnly).
            auto lines = lines::FurnitureOnly(obj::ClassName(widget)) ? std::vector<std::wstring>{} : ui::DrawnRows(widget);
            const auto prompts = ui::PromptsIn(widget);
            // A hint of one line with a bare key beside it (a stick next to "Walk around the
            // kitchen") is one prompt: the line names the key, where it has words (lines::NamesBareKey).
            if (lines.size() == 1 && lines::NamesBareKey(lines.front()) && !prompts.empty() &&
                std::all_of(prompts.begin(), prompts.end(), [](const ui::Prompt& p) { return p.label.empty() && !p.key.empty(); }))
            {
                return {lines.front() + L": " + ui::PromptLines(prompts)};
            }
            for (const auto& prompt : prompts)
            {
                const auto line = ui::PromptLines({prompt});
                if (!line.empty()) lines.push_back(line);
            }
            return lines;
        }

        std::wstring Readout(UObject* widget)
        {
            return str::JoinSentences(Lines(widget));
        }

        void Track(UObject* widget)
        {
            if (!widget || !obj::IsLive(widget)) return;
            if (std::any_of(g_tracked.begin(), g_tracked.end(), [&](const Tracked& t) { return t.instance == widget; })) return;
            // The front end's furniture (its footer, its backgrounds) belongs to the menus; the
            // HUD is read in play, the loading screen and the words over a film whenever they come.
            if (!gamestate::InPlay() && !gamestate::Paused() && !obj::IsA(widget, L"BatesLoadingScreen") && !ui::IsVideoText(widget)) return;
            const auto cls = obj::ClassName(widget);
            // Whatever a menu's container draws (the pause menu's footer, the examples beside
            // the settings) is the menu's; the menus read it, or leave it, as they read the
            // same thing in the front end.
            if (ui::InsideMenuHost(widget) || ui::InsideMenuPage(widget))
            {
                if (g_classesSeen.insert(cls).second) log::Verbose(L"hud: {} belongs to a menu, left to the menus", cls);
                return;
            }
            if (g_classesSeen.insert(cls).second) log::Info(L"hud: {} first shown as {}, says \"{}\"", cls, obj::ObjectName(widget), Readout(widget));
            if (obj::IsA(widget, L"BatesLoadingScreen"))
            {
                speech::Announce(locale::Mod(L"hud.loading"));
                return;
            }
            g_tracked.push_back(Tracked{widget});
        }

        // An element is read once its words have stopped changing, line by new line
        // (lines::Element).
        double g_settlingSince = -1.0;      // since when a shown element's words are still to be said
        double g_saidAt = -1000.0;          // when an element's words were last said
        double g_documentDrawnAt = -1000.0; // when a document's transcript was last drawn

        // A clue's card and the page it comes with, said together, the card first (lines::CardFirst);
        // a page shown again as it is being read, the player having done nothing since it went
        // (lines::LeaveShownAgain), is not said again, and the prompts the bar shows with it again
        // are not either (PageResumedAt).
        lines::CardFirst g_cardFirst;
        double g_titleFillUntil = -1.0;   // a title card still fades its words in (lines::TitleFill)
        double g_pageComingUntil = -1.0;  // a reading's page is still to come (lines::PageComing)
        std::wstring g_pageText;          // the page's own words, as last said
        double g_pageSaidAt = -1000.0;    // when they were
        double g_pageResumedAt = -1000.0; // when a page was shown again while it was being read
        bool g_pageWasDrawn = false;      // a page was drawn at the last poll
        double g_pageHiddenAt = -1000.0;  // when the page drawn last was last seen before it went

        bool IsCard(UObject* widget)
        {
            return obj::IsA(widget, L"BatesHUDUpdateQueueWidget");
        }

        // The cards that name a chapter, introduce a character or change to another.
        bool IsTitleCard(UObject* widget)
        {
            return obj::IsA(widget, L"BatesChapterIntroWidget") || obj::IsA(widget, L"BatesCharacterIntroWidget") ||
                   obj::IsA(widget, L"BatesCharacterChangeWidget");
        }

        bool IsPage(UObject* widget)
        {
            return obj::IsA(widget, L"BatesReadingWidget");
        }

        // A document's page to say: the cards that came with it first (lines::PageWords). A page shown
        // again unchanged while the reader may still be on it, the player having done nothing since it
        // went, is left as it is said (lines::LeaveShownAgain), and only its cards are said, behind it.
        void SayPage(const std::vector<std::wstring>& cards, const std::wstring& page, bool echoed, double now)
        {
            const bool same = page == g_pageText;
            const bool input = static_cast<double>(input::MsSinceInput()) / 1000.0 < now - g_pageHiddenAt;
            const bool leave = same && lines::LeaveShownAgain(speech::StillOn(g_pageSaid), echoed, input);
            const auto words = lines::PageWords(cards, page, leave);
            if (leave)
            {
                g_pageResumedAt = now;
                // The reading goes on: the next page cuts this one short (lines::Reading).
                g_reading.Interrupts();
                log::Info(L"hud: the page is shown again as it is being read: not said again{}",
                          words.empty() ? L"" : L"; the card that came with it is said behind it");
                if (!words.empty()) speech::Announce(str::JoinSentences(words));
                return;
            }
            if (same) log::Info(L"hud: the page is shown again {}: said again", input ? L"after the player's input" : L"after it was read");
            const auto text = str::JoinSentences(words);
            if (!cards.empty()) log::Info(L"hud: the page is said with the card that came with it, the card first");
            // A document's page in place of the one before cuts it short (lines::Reading), and only it
            // (speech::Replace).
            if (g_reading.Interrupts())
                speech::Replace(g_pageSaid, text);
            else
                speech::Announce(text);
            g_pageSaid = text;
            g_pageText = page;
            g_pageSaidAt = now;
        }

        void PollImpl()
        {
            if (gamethread::FrameCount() % 6 != 0) return;
            const double now = gamethread::NowSeconds();
            if (ExplorationActive()) g_reading.Ended();
            bool settling = false;
            std::erase_if(g_tracked, [](const Tracked& t) { return !obj::IsLive(t.instance); });
            struct Fresh
            {
                Tracked* tracked = nullptr;
                std::vector<std::wstring> lines;
                std::wstring text;
            };
            std::vector<Fresh> cards;
            std::vector<Fresh> pages;
            bool cardComing = false; // a card shown whose words are still to come or to be said
            bool pageComing = false; // the reading's page still to come: hidden as it turns, or not said yet
            bool titleFilling = false;
            const double drawnBefore = g_documentDrawnAt; // when a page was last drawn, before this poll
            bool pageDrawn = false;
            for (auto& t : g_tracked)
            {
                if (!obj::IsWidgetShown(t.instance))
                {
                    t.lines.Hidden();
                    // A card taken away before its body came is said as it stood (lines::Card::Hidden);
                    // a pause hides the HUD for a while and is no taking away.
                    const bool paused = gamestate::Paused();
                    if (!paused && t.card.Hidden(now) && !t.cardTitle.empty())
                    {
                        log::Info(L"hud: {} went before its body was drawn; said as it stood: \"{}\"", obj::ClassName(t.instance), t.cardTitle);
                        g_saidAt = now;
                        if (g_echo.Says(t.cardTitle, now)) speech::Announce(t.cardTitle);
                    }
                    if (!paused) t.cardTitle.clear();
                    t.fill.Hidden();
                    t.clock.Hidden();
                    if (IsPage(t.instance) && lines::PageComing(g_reading.Open(), false, now - drawnBefore, false)) pageComing = true;
                    continue;
                }
                // An update card of the game's queue is read once its body is drawn (lines::Card).
                UObject* body = nullptr;
                if (IsCard(t.instance) && obj::ReadObject(t.instance, L"Text_Body", body) && obj::IsLive(body) &&
                    !t.card.Ready(now, !ui::DrawnTexts(body).empty() && obj::IsWidgetShown(body, true)))
                {
                    cardComing = true;
                    if (obj::IsWidgetShown(t.instance, true))
                    {
                        if (auto title = str::JoinSentences(Lines(t.instance)); !title.empty()) t.cardTitle = std::move(title);
                    }
                    continue;
                }
                if (IsCard(t.instance)) t.cardTitle.clear();
                // A title card still fading its words in holds the prompts (lines::TitleFill).
                if (IsTitleCard(t.instance) && t.fill.Update(now, ui::VisibleTexts(t.instance), ui::DrawnTexts(t.instance))) titleFilling = true;
                // Faded out to nothing, it shows no lines (a document's transcript hidden).
                const bool drawn = obj::IsWidgetShown(t.instance, true);
                if (drawn && IsPage(t.instance))
                {
                    g_documentDrawnAt = now;
                    pageDrawn = true;
                }
                const auto lines = Lines(t.instance);
                // A clock ticking on is said as its hours and minutes change (lines::ClockTicks); a
                // document's page and a card are held and given back whole (lines::CardFirst), and tick
                // no clock.
                const auto shown = t.lines.Shown(lines, now, drawn);
                const auto fresh = IsPage(t.instance) || IsCard(t.instance) ? shown : t.clock.Filter(shown, now);
                const bool unsaid = drawn && !lines.empty() && t.lines.Unsaid(lines, now);
                if (fresh.empty())
                {
                    if (unsaid) settling = true;
                    if (unsaid && IsCard(t.instance)) cardComing = true;
                    if (IsPage(t.instance) && lines::PageComing(g_reading.Open(), drawn, now - drawnBefore, unsaid)) pageComing = true;
                    continue;
                }
                Fresh f{&t, fresh, str::JoinSentences(fresh)};
                if (IsPage(t.instance))
                    pages.push_back(std::move(f));
                else if (IsCard(t.instance))
                    cards.push_back(std::move(f));
                else
                {
                    log::Info(L"hud: {} says \"{}\"", obj::ClassName(t.instance), f.text);
                    g_saidAt = now;
                    if (g_echo.Says(f.text, now)) speech::Announce(f.text);
                }
            }
            // The page still to come holds a step's prompts (hud::PageComing), lapsing by itself.
            if (pageComing) g_pageComingUntil = now + lines::kHoldLapse;
            // The hold lapses by itself a poll's while after the last poll that found the card filling.
            if (titleFilling)
            {
                if (g_titleFillUntil < now) log::Verbose(L"hud: a title card fades its words in: the prompts wait for it");
                g_titleFillUntil = now + lines::kHoldLapse;
            }
            // The page drawn at the last poll is gone: when it was last seen, for what is done after.
            if (g_pageWasDrawn && !pageDrawn) g_pageHiddenAt = drawnBefore;
            g_pageWasDrawn = pageDrawn;
            // A page waits for the card still coming with it, a card for the page still coming: each
            // given back to its element to be read again at the next poll (lines::CardFirst). A card up
            // long already waits for nothing.
            bool holding = false;
            if (!pages.empty() && cards.empty() && g_cardFirst.HoldPage(now, cardComing))
            {
                for (auto& f : pages)
                    f.tracked->lines.Unsay(f.lines);
                pages.clear();
                holding = true;
            }
            if (!cards.empty() && pages.empty())
            {
                double up = 0.0;
                for (const auto& f : cards)
                    up = std::max(up, f.tracked->card.ShownFor(now));
                if (g_cardFirst.HoldCard(now, pageComing, up))
                {
                    for (auto& f : cards)
                        f.tracked->lines.Unsay(f.lines);
                    cards.clear();
                    holding = true;
                }
            }
            if (holding)
                settling = true;
            else
                g_cardFirst.Settled();
            // What is said now, each said once though it came through two elements (lines::Echo).
            std::vector<std::wstring> cardTexts;
            for (const auto& f : cards)
            {
                log::Info(L"hud: {} says \"{}\"", obj::ClassName(f.tracked->instance), f.text);
                g_saidAt = now;
                if (g_echo.Says(f.text, now)) cardTexts.push_back(f.text);
            }
            bool pageSaid = false;
            for (const auto& f : pages)
            {
                // The page through a second element at once is the page.
                if (pageSaid && f.text == g_pageText) continue;
                log::Info(L"hud: {} says \"{}\"", obj::ClassName(f.tracked->instance), f.text);
                g_saidAt = now;
                // Other words through another element at once are said once (lines::Echo); the page said
                // last, shown again, is SayPage's to judge.
                const bool echoed = !g_echo.Says(f.text, now);
                if (echoed && f.text != g_pageText) continue;
                SayPage(pageSaid ? std::vector<std::wstring>{} : cardTexts, f.text, echoed, now);
                pageSaid = true;
            }
            if (!pageSaid)
            {
                for (const auto& text : cardTexts)
                    speech::Announce(text);
            }
            if (!settling)
                g_settlingSince = -1.0;
            else if (g_settlingSince < 0.0)
                g_settlingSince = now;
        }

        // The objective the game keeps for the player, set by the scene's script and shown
        // on the status page ("Find Hannah"): said when it changes, once play runs with no title
        // card up (lines::Objective), and read with the screen.
        lines::Objective g_objective;

        std::wstring CurrentObjective()
        {
            UObject* manager = obj::FindFirstLive(L"BatesTraitsRelationshipsManager");
            if (!manager) return {};
            return str::CollapseWhitespace(obj::CallForText(manager, L"GetCurrentObjective"));
        }

        // The cards that name a chapter, introduce a character or change to another: the chapter's
        // card from the moment the game gives it its number (it is drawn a moment later, its words
        // fading in), every card while the HUD's watch sees it shown.
        const wchar_t* const kTitleCards[] = {L"BatesChapterIntroWidget", L"BatesCharacterIntroWidget", L"BatesCharacterChangeWidget"};
        obj::ObjectKey g_chapterCard; // by its identity: a freed slot reused is not the card
        double g_chapterCardAt = -100.0;
        constexpr double kCardDrawnWithin = 2.0;

        void OnChapterNumber(UObject* self, FFrame&)
        {
            g_chapterCard = obj::KeyOf(self);
            g_chapterCardAt = gamethread::NowSeconds();
            log::Verbose(L"hud: {} is given its chapter's number", obj::ObjectName(self));
        }

        bool TitleCardUp(double now)
        {
            if (obj::IsSame(g_chapterCard) && (obj::IsWidgetShown(g_chapterCard.object) || now - g_chapterCardAt < kCardDrawnWithin)) return true;
            for (const auto& t : g_tracked)
            {
                if (!obj::IsLive(t.instance) || !obj::IsWidgetShown(t.instance)) continue;
                for (const wchar_t* cls : kTitleCards)
                {
                    if (obj::IsA(t.instance, cls)) return true;
                }
            }
            return false;
        }

        // The card that changes the character (name, place, time) is up.
        bool CharacterCardUp()
        {
            for (const auto& t : g_tracked)
            {
                if (obj::IsLive(t.instance) && obj::IsWidgetShown(t.instance) && obj::IsA(t.instance, L"BatesCharacterChangeWidget")) return true;
            }
            return false;
        }

        obj::ObjectKey g_objectivePawn; // the player's character the objective was last weighed for

        void PollObjective()
        {
            if (gamethread::FrameCount() % 30 != 7) return;
            const double now = gamethread::NowSeconds();
            const bool known = g_objective.Known();
            // A turn opens at a level's load, or when the player is given another character; its
            // objective waits for the card that names her, or for the first hand-over
            // (lines::Objective).
            UObject* pawn = world::Pawn();
            const bool newCharacter = pawn && !(obj::KeyOf(pawn) == g_objectivePawn);
            if (pawn) g_objectivePawn = obj::KeyOf(pawn);
            if (gamestate::Current() == gamestate::State::LoadingNewLevel || newCharacter)
            {
                if (!g_objective.Opens()) log::Verbose(L"hud: a turn opens: a new objective waits for the character's card or the hand-over");
                g_objective.Opening();
            }
            if (CharacterCardUp()) g_objective.Card();
            if (ExplorationActive()) g_objective.HandedOver();
            const bool free = gamestate::InPlay() && !TitleCardUp(now);
            const auto step = g_objective.Update(now, CurrentObjective(), free);
            if (step.changed)
            {
                if (g_objective.Current().empty())
                {
                    if (known) log::Info(L"hud: the objective is turned off");
                }
                else
                {
                    log::Info(L"hud: the objective is \"{}\"{}", g_objective.Current(),
                              !step.say.empty()     ? L""
                              : g_objective.Opens() ? L", said once the character's card has come and gone, or she is handed over"
                                                    : L", said once play runs with no title card up");
                }
            }
            if (step.say.empty()) return;
            if (!step.changed) log::Info(L"hud: the objective \"{}\" is said now", step.say);
            speech::Announce(locale::Mod(L"hud.objective", step.say));
        }

        // The loading screen's bar: the share of the level's streamed parts already in, as
        // the screen draws it (UBatesLoadingScreen::NativeTick hands it to its Blueprint every
        // frame). It is full long before the screen goes when the game compiles shaders
        // behind it.
        UObject* g_loadingScreen = nullptr;
        double g_loadingShare = -1.0;

        void OnLoadingShare(UObject* self, FFrame& stack)
        {
            double share = 0.0;
            if (!params::Float(stack, L"InPerc", share)) return;
            g_loadingScreen = self;
            g_loadingShare = std::clamp(share, 0.0, 1.0);
        }

        bool LoadingScreenShown()
        {
            return g_loadingScreen && obj::IsLive(g_loadingScreen) && obj::IsWidgetShown(g_loadingScreen);
        }

        // While a level loads, what it waits for, now and then, for the log: the bar; the
        // streaming manager's flags (2 a streaming request pending, 4 character variants
        // loading, 8 shaders compiling), which must all clear before the game turns to play;
        // and the shader compile behind the bar: the game's shader cache's mode (0 once its
        // own stall check has paused the compile, which only a change of the loading screen's
        // phase resumes) and the pipelines left of those it began with.
        double g_loadingLoggedAt = -100.0;

        void PollLoading()
        {
            const auto state = gamestate::Current();
            if (state != gamestate::State::LoadingNewLevel && state != gamestate::State::Loading)
            {
                g_loadingLoggedAt = -100.0;
                return;
            }
            const double now = gamethread::NowSeconds();
            if (g_loadingLoggedAt < 0.0) g_loadingLoggedAt = now;
            if (now - g_loadingLoggedAt < 15.0) return;
            g_loadingLoggedAt = now;
            int flags = -1;
            int mode = -1;
            int total = -1;
            int remaining = -1;
            if (UObject* streaming = obj::FindFirstLive(L"BatesLevelStreamingManager"))
            {
                if (const auto* p = static_cast<const uint8_t*>(sym::MemberOf(streaming, L"UBatesLevelStreamingManager.StreamingFlags"))) flags = *p;
            }
            if (UObject* shaders = obj::FindFirstLive(L"BatesShaderCacheSubsystem"))
            {
                if (const auto* p = static_cast<const int32_t*>(sym::MemberOf(shaders, L"UBatesShaderCacheSubsystem.CompilationMode"))) mode = *p;
                if (const auto* p = static_cast<const int32_t*>(sym::MemberOf(shaders, L"UBatesShaderCacheSubsystem.PrecompilesTotal"))) total = *p;
            }
            using Count = int32_t (*)();
            if (auto* count = reinterpret_cast<Count>(sym::Address(L"FShaderPipelineCache::NumPrecompilesRemaining"))) remaining = count();
            log::Info(L"loading: the bar at {}, the screen {}; streaming flags {} (2 a request pending, 4 character variants, 8 shaders); shader cache mode {} "
                      L"(0 paused), {} of {} pipelines left to compile",
                      g_loadingShare < 0.0 ? std::wstring(L"unknown") : std::format(L"{:.0f}%", g_loadingShare * 100.0),
                      LoadingScreenShown() ? L"shown" : L"not shown", flags < 0 ? std::wstring(L"unknown") : std::format(L"{:#x}", flags), mode, remaining,
                      total);
        }

        void Poll(float)
        {
            obj::SafeInvokeLogged(L"hud.PollImpl", [](void*) { PollImpl(); }, nullptr);
            obj::SafeInvokeLogged(L"hud.PollObjective", [](void*) { PollObjective(); }, nullptr);
            obj::SafeInvokeLogged(L"hud.PollLoading", [](void*) { PollLoading(); }, nullptr);
        }
    }

    void HudFeature::Install()
    {
        // The elements of the HUD as the watcher finds them; those read elsewhere are left.
        watch::OnWidgetClass(L"HudElement",
                             [](UObject* widget, bool appeared)
                             {
                                 if (appeared && !OwnedElsewhere(widget)) Track(widget);
                             });
        hooks::OnScript(L"BatesLoadingScreen", L"BP_SetPercLoaded", &OnLoadingShare);
        hooks::OnScript(L"BatesChapterIntroWidget", L"BP_SetChapterNumber", &OnChapterNumber);
        gamethread::AddPoller(L"hud", &Poll);
    }

    void HudFeature::Describe(std::vector<std::wstring>& out)
    {
        // The loading screen with its bar, as a glance at it shows; its own word is then in the
        // line already.
        const bool bar = LoadingScreenShown() && g_loadingShare >= 0.0;
        if (bar) out.push_back(locale::Mod(L"hud.loading.share", locale::Mod(L"hud.loading"), std::to_wstring(static_cast<int>(g_loadingShare * 100.0 + 0.5))));
        for (const auto& t : g_tracked)
        {
            if (bar && t.instance == g_loadingScreen) continue;
            if (obj::IsLive(t.instance) && obj::IsWidgetShown(t.instance) && !t.lines.Seen().empty()) out.push_back(str::JoinSentences(t.lines.Seen()));
        }
        if (gamestate::InPlay() && !g_objective.Current().empty()) out.push_back(locale::Mod(L"hud.objective", g_objective.Current()));
    }

    bool hud::TextSettling(double now)
    {
        // A page waiting for its card, or a card for its page, holds the prompts as long as it waits
        // (lines::CardFirst): the page and its card come before the stick's prompt and the way back.
        return (g_settlingSince >= 0.0 && now - g_settlingSince < kHintFirst) || g_cardFirst.Holding(now);
    }

    double hud::PageResumedAt()
    {
        return g_pageResumedAt;
    }

    double hud::PageSaidAt()
    {
        return g_pageSaidAt;
    }

    bool hud::TitleCardFilling(double now)
    {
        return now < g_titleFillUntil;
    }

    bool hud::PageComing(double now)
    {
        return now < g_pageComingUntil;
    }

    double hud::LastSaidAt()
    {
        return g_saidAt;
    }

    std::wstring hud::Objective()
    {
        return g_objective.Current();
    }

    double hud::DocumentDrawnAt()
    {
        return g_documentDrawnAt;
    }

    bool hud::CharacterCardShown()
    {
        return CharacterCardUp();
    }

    bool hud::ObjectiveDue()
    {
        return g_objective.Due();
    }
}
