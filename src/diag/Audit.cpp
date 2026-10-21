#include "diag/Audit.hpp"

#include "core/GameThread.hpp"
#include "core/Gate.hpp"
#include "core/Log.hpp"
#include "core/ObjectUtil.hpp"
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
                {
                    Drawn& d = g_drawn[text];
                    if (d.firstAt == 0.0) d.firstAt = now;
                    d.lastAt = now;
                    if (!d.said && speech::SaidRecently(text, kSaidWithin)) d.said = true;
                    if (d.said || d.reported || now - d.firstAt < kDrawnFor) continue;
                    d.reported = true;
                    log::Info(L"unread: {} \"{}\"", cls, text);
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
