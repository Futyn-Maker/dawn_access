#include "features/Subtitles.hpp"

#include "core/Config.hpp"
#include "core/GameThread.hpp"
#include "core/Log.hpp"
#include "core/ObjectUtil.hpp"
#include "core/Strings.hpp"
#include "hooks/HookDispatcher.hpp"
#include "hotkeys/Hotkeys.hpp"
#include "input/InputNames.hpp"
#include "locale/Locale.hpp"
#include "speech/Speech.hpp"
#include "watch/Watchers.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace da::features
{
    using RC::Unreal::FFrame;
    using RC::Unreal::UObject;

    namespace
    {
        std::vector<UObject*> g_widgets;         // the subtitle widgets the game shows
        std::vector<std::wstring> g_shown;       // the lines on screen at the last poll
        std::vector<std::wstring> g_shownBefore; // and at the poll before that
        std::wstring g_lastLine;                 // the last line that appeared
        bool g_read = true;                      // whether lines are spoken as they appear

        // One subtitle line as displayed: the speaker's name when the game shows it, then the
        // words, each read from the text block the game draws it in.
        std::wstring LineText(UObject* line)
        {
            if (!line || !obj::IsLive(line) || !obj::IsWidgetShown(line)) return {};
            std::wstring dialogue;
            UObject* block = nullptr;
            if (obj::ReadObject(line, L"Text_Dialogue", block) && block && obj::IsLive(block) && obj::IsWidgetVisible(block))
                dialogue = str::Trim(str::StripMarkup(obj::TextOf(block)));
            if (dialogue.empty()) return {};
            std::wstring name;
            if (obj::ReadObject(line, L"Text_CharacterName", block) && block && obj::IsLive(block) && obj::IsWidgetVisible(block))
                name = str::Trim(str::StripMarkup(obj::TextOf(block)));
            return name.empty() ? dialogue : name + L": " + dialogue;
        }

        void Track(UObject* widget)
        {
            if (!widget || !obj::IsLive(widget)) return;
            if (std::find(g_widgets.begin(), g_widgets.end(), widget) != g_widgets.end()) return;
            g_widgets.push_back(widget);
            log::Info(L"subtitles: {} {} tracked", obj::ClassName(widget), obj::ObjectName(widget));
        }

        bool Contains(const std::vector<std::wstring>& lines, const std::wstring& text)
        {
            return std::find(lines.begin(), lines.end(), text) != lines.end();
        }

        // Every frame, so that a line is read the moment it appears: the lines on screen are
        // compared with those of the two polls before, and only new text is spoken. Two new
        // lines in one frame are one subtitle wrapped over two lines, or two speakers at
        // once; they are read together.
        void PollImpl()
        {
            std::erase_if(g_widgets, [](UObject* w) { return !obj::IsLive(w); });
            if (g_widgets.empty()) return;
            std::vector<std::wstring> shown;
            std::vector<std::wstring> fresh;
            for (UObject* widget : g_widgets)
            {
                if (!obj::IsWidgetShown(widget)) continue;
                for (const wchar_t* name : {L"Subtitle_0", L"Subtitle_1"})
                {
                    UObject* line = nullptr;
                    if (!obj::ReadObject(widget, name, line)) continue;
                    const auto text = LineText(line);
                    if (text.empty()) continue;
                    if (!Contains(g_shown, text) && !Contains(g_shownBefore, text) && !Contains(fresh, text)) fresh.push_back(text);
                    shown.push_back(text);
                }
            }
            g_shownBefore = std::move(g_shown);
            g_shown = std::move(shown);
            if (fresh.empty()) return;
            g_lastLine = str::Join(fresh, L" ");
            log::Info(L"subtitles: line \"{}\"", g_lastLine);
            if (g_read) speech::Announce(g_lastLine);
        }

        void Poll(float)
        {
            obj::SafeInvokeLogged(L"subtitles.PollImpl", [](void*) { PollImpl(); }, nullptr);
        }
    }

    void SubtitlesFeature::Install()
    {
        g_read = cfg::Get().readSubtitles;
        log::Info(L"subtitles: reading {} at start", g_read ? L"on" : L"off");
        // The widget announces its own set-up; the scan of the screen catches one set up
        // before the mod was listening.
        hooks::OnScript(L"BatesSubtitlesWidget", L"BP_Setup", [](UObject* self, FFrame&) { Track(self); });
        watch::OnWidgetClass(L"BatesSubtitlesWidget",
                             [](UObject* widget, bool appeared)
                             {
                                 if (appeared) Track(widget);
                             });
        hotkeys::OnCommand(hotkeys::Command::Subtitles,
                           []()
                           {
                               ToggleSubtitles();
                               return true;
                           });
        hotkeys::OnCommand(hotkeys::Command::LastSubtitle,
                           []()
                           {
                               SpeakLastSubtitle();
                               return true;
                           });
        gamethread::AddPoller(L"subtitles", &Poll);
    }

    void SubtitlesFeature::Describe(std::vector<std::wstring>& out)
    {
        if (!g_shown.empty()) out.push_back(str::Join(g_shown, L" "));
    }

    void SubtitlesFeature::Help(std::vector<std::wstring>& out)
    {
        using hotkeys::Command;
        if (hotkeys::ChordsInHand())
            out.push_back(locale::Mod(L"help.subtitles.pad", std::vector<std::wstring>{input::KeyDisplayName(hotkeys::ChordHold()),
                                                                                       input::KeyDisplayName(hotkeys::ChordOf(Command::Subtitles)),
                                                                                       input::KeyDisplayName(hotkeys::ChordOf(Command::LastSubtitle))}));
        else
            out.push_back(locale::Mod(L"help.subtitles", hotkeys::KeyOf(Command::Subtitles), hotkeys::KeyOf(Command::LastSubtitle)));
    }

    void ToggleSubtitles()
    {
        SetSubtitles(!g_read);
        speech::Now(locale::Mod(g_read ? L"subtitles.on" : L"subtitles.off"));
    }

    void SetSubtitles(bool read)
    {
        g_read = read;
        log::Info(L"subtitles: reading {}", g_read ? L"on" : L"off");
        // Kept in the ini, so that the next start begins the way this one ended.
        if (!cfg::Persist(L"Subtitles", L"Read", g_read ? L"1" : L"0")) log::Error(L"subtitles: the setting could not be saved to DawnAccess.ini");
    }

    void SpeakLastSubtitle()
    {
        speech::Now(g_lastLine.empty() ? locale::Mod(L"subtitles.none") : g_lastLine);
    }

    void ResetSubtitles()
    {
        g_lastLine.clear();
        g_shown.clear();
        g_shownBefore.clear();
    }
}
