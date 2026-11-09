#pragma once
#include <string>
// The game's own menu narration, written to the log as the game queues and speaks it: the
// words it would read are compared with the mod's and adopted where they are better. Nothing
// is spoken from here, and the game's voice stays as the player set it.

namespace RC::Unreal
{
    class UObject;
}

namespace da::narration
{
    void Install();

    // What the game's own narration last said of a control (at its button priority), every line
    // of the utterance, when it queued one within the given seconds: its evaluation of the
    // control's data, such as the locked word for a collectible not yet earned. Empty otherwise.
    // `nameless`, when asked, tells whether the game left the control's name out of it
    // (menu::Utterance::Nameless).
    std::wstring LabelFor(RC::Unreal::UObject* owner, double maxAgeSeconds, bool* nameless = nullptr);

    // The name the game's narration gave a page it announced as a menu, for a page that draws
    // no title of its own. Empty when it gave none.
    std::wstring PageNameFor(RC::Unreal::UObject* page);
}
