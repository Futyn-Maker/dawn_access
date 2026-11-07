#pragma once
// What the speech policies decide, worked out without the reader so that it can be tested.

#include <string>
#include <vector>

namespace da::speech::rules
{
    // A line given to the reader, with the policy it went by ("announce", "now", ...) and when, in
    // seconds of a steady clock.
    struct Said
    {
        std::wstring policy;
        std::wstring text;
        double at = 0.0;
    };

    // Whether a line went to the reader cutting short what it was saying; the reader silenced
    // ("stop": the player's key, F7) counts as one.
    bool Interrupting(const std::wstring& policy);

    // How fast the reader is taken to speak, in characters a second, for what it can have finished:
    // brisk, as a screen reader is set by those who live by it. NVDA reads no word back on when it is
    // done (Prism's NVDA backend has no is_speaking), so it is reckoned. Andrey's NVDA had said a
    // memo of 640 characters and the two short lines queued behind it in the 37 s before he turned
    // its page (research/sessions/s52-full.log, 18:35:09 to 18:35:46): at least 19 a second.
    constexpr double kReaderPace = 20.0;

    // What a document's page turned does to what the reader is saying (Replace): it cuts the page
    // before short, and only it. While the reader may still be on that page at kReaderPace (`cut`),
    // the new page cuts in, and the lines queued behind the old page since it was given to the
    // reader, with no line cutting in since (what the game said by itself meanwhile: a clue's card, a
    // line of dialogue), are said again before the new page, in their order (`kept`): none of them
    // was begun. Once the reader is past the page the new one cuts nothing: it waits behind what the
    // reader is saying or has yet to say. `said` is what went to the reader, oldest first; `page` the
    // page's text as it was said. The page not among them, or a line cut in after it, the page is gone
    // already, and the new one cuts in with nothing kept. The business card at the lodge on
    // 2026-11-07 was turned 13 s after its front was read, as the clue's card was being said: cut in
    // and said again whole, the card was heard twice, the second cutting the first
    // (research/sessions/s56-full.log, 23:52:59).
    struct TurnPlan
    {
        bool cut = true;
        std::vector<std::wstring> kept;
    };
    TurnPlan Turn(const std::vector<Said>& said, const std::wstring& page, double now);

    // The seconds the reader takes over a line at kReaderPace.
    double Speaking(const std::wstring& text);
}
