#pragma once
// Subtitles: every line the game's subtitle widget shows is read as it appears, exactly as
// displayed, with the speaker's name when the game shows one. Reading can be switched off
// and the last line repeated with the keys of the hotkey table; the switch is kept in the ini.

#include "features/Feature.hpp"

#include <string>
#include <vector>

namespace da::features
{
    class SubtitlesFeature : public Feature
    {
    public:
        const wchar_t* Name() const override
        {
            return L"Subtitles";
        }
        void Install() override;
        void Describe(std::vector<std::wstring>& out) override;
        void Help(std::vector<std::wstring>& out) override;
    };

    void ToggleSubtitles();
    void SetSubtitles(bool read);
    void SpeakLastSubtitle();
    // Forgets the last line (after a map load, when a line from before is not worth repeating).
    void ResetSubtitles();
}
