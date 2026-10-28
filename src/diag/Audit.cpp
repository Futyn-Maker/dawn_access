#include "diag/Audit.hpp"

#include "core/GameThread.hpp"
#include "core/Gate.hpp"
#include "core/Log.hpp"
#include "core/ObjectUtil.hpp"
#include "core/Symbols.hpp"
#include "speech/Speech.hpp"
#include "ui/Widgets.hpp"
#include "watch/Watchers.hpp"

#include <map>
#include <string>

namespace da::diag
{
    using RC::Unreal::UObject;

    namespace
    {
        // A text is judged once it has been drawn this long (the features take up to a second
        // or two to read a settled text), against what was said within this window; a text
        // gone this long is forgotten, so that its return is judged anew.
        constexpr double kDrawnFor = 4.0;
        constexpr double kSaidWithin = 60.0;
        constexpr double kForgetAfter = 5.0;

        // Elements whose texts are left unread on purpose, or read by their own rules.
        const wchar_t* const kLeftAlone[] = {
            L"BatesSubtitlesWidget", // its feature logs every line; the player may have its reading off
            L"BatesVersionWidget",   // the build number in the corner
        };

        struct Drawn
        {
            double firstAt = 0.0;
            double lastAt = 0.0;
            bool said = false;
            bool reported = false;
        };
        std::map<std::wstring, Drawn> g_drawn;

        bool LeftAlone(UObject* widget)
        {
            for (const wchar_t* cls : kLeftAlone)
            {
                if (obj::IsA(widget, cls)) return true;
            }
            return false;
        }

        void Note(const std::wstring& cls, const std::wstring& text, double now, const wchar_t* kind)
        {
            Drawn& d = g_drawn[text];
            if (d.firstAt == 0.0) d.firstAt = now;
            d.lastAt = now;
            if (!d.said && speech::SaidRecently(text, kSaidWithin)) d.said = true;
            if (d.said || d.reported || now - d.firstAt < kDrawnFor) return;
            d.reported = true;
            log::Info(L"unread{}: {} \"{}\"", kind, cls, text);
        }

        void PollImpl()
        {
            // Between two scans of the screen watcher.
            if (gamethread::FrameCount() % 30 != 15) return;
            if (!gate::IsOn()) return;
            const double now = gamethread::NowSeconds();
            for (const auto& [cls, widget] : watch::LiveHudInstances())
            {
                if (!widget || !obj::IsLive(widget) || LeftAlone(widget)) continue;
                for (const auto& text : ui::AuditTexts(widget))
                    Note(cls, text, now, L"");
            }
            // The prompts the game draws on screen layers of their own, in no widget tree:
            // a glint's key once its prompt is up, the keys on the character's wrist in an
            // interaction. Their words are their keys.
            for (const wchar_t* cls : {L"BatesInteractionWidget", L"BatesGlintWidget"})
            {
                for (UObject* widget : obj::FindAllLive(cls))
                {
                    const bool shown = obj::IsWidgetShown(widget);
                    if (!shown && !ui::LayerWidgetDrawn(widget)) continue;
                    if (obj::IsA(widget, L"BatesGlintWidget"))
                    {
                        // A glint's prompt by the widget's own flags (shown, faded in, not going
                        // away), as the prompt reader takes it; its cap's opacity without them.
                        const auto* showing = static_cast<const uint8_t*>(sym::MemberOf(widget, L"UBatesGlintWidget.bIsShowingPrompt"));
                        const auto* destroying = static_cast<const uint8_t*>(sym::MemberOf(widget, L"UBatesGlintWidget.bIsDestroying"));
                        const auto* faded = static_cast<const float*>(sym::MemberOf(widget, L"UBatesGlintWidget.CurrentShowPrompt"));
                        if (showing && destroying && faded)
                        {
                            if (*showing == 0 || *destroying != 0 || *faded < 0.99f) continue;
                        }
                        else
                        {
                            UObject* cap = nullptr;
                            if (!obj::ReadObject(widget, L"InputActionWidget", cap) || !obj::IsLive(cap) || !obj::IsWidgetVisible(cap) ||
                                obj::WidgetOpacity(cap) < 0.6)
                                continue;
                        }
                    }
                    const auto text = ui::PromptLines(ui::PromptsIn(widget, !shown));
                    if (!text.empty()) Note(obj::ClassName(widget), text, now, L" prompt");
                }
            }
            for (auto it = g_drawn.begin(); it != g_drawn.end();)
            {
                if (now - it->second.lastAt > kForgetAfter)
                    it = g_drawn.erase(it);
                else
                    ++it;
            }
        }

        void Poll(float)
        {
            obj::SafeInvokeLogged(L"audit.PollImpl", [](void*) { PollImpl(); }, nullptr);
        }
    }

    void InstallUnreadAudit()
    {
        gamethread::AddPoller(L"audit", &Poll);
    }
}
