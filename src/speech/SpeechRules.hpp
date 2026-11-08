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
    // line of dialogue, the stick's prompt), are said again after the new page, in their order
    // (`kept`): none of them was begun. The new page first: Andrey turned Hannah's diary on
    // 2026-11-08 and heard the stick's prompt and the way back before each page's text, and asked for
    // the text first (research/sessions/s58-whole.log, 11:23:09). Once the reader is past the page the new one cuts nothing: it waits behind what the
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

    // The lines a page turned gives the reader, in order, and whether each cuts in: the new page,
    // cutting the old one short where the plan cuts it, then what was kept, queued behind it.
    struct TurnLine
    {
        std::wstring text;
        bool interrupt = false;
        std::wstring policy; // as logged: "replace", "replace-after" (behind what is said), "replace-kept"
    };
    std::vector<TurnLine> TurnOrder(const TurnPlan& plan, const std::wstring& page);

    // The seconds the reader takes over a line at kReaderPace.
    double Speaking(const std::wstring& text);

    // Whether the reader may still be on a page given to it: the page as last said, with nothing
    // cutting in after it, and less time gone since than it takes to say. A document's page the game
    // shows again unchanged is then not said again: in act7_1 Ashley's comment on Jack's journal hid
    // its transcript for 3 s, 7 s into a page of 700 characters, and showed it again; cut in and said
    // again, the page was heard to its middle and then from its start, with the prompts behind it
    // twice (research/sessions/s61-play.log, 18:59:39 to 18:59:50).
    bool StillOn(const std::vector<Said>& said, const std::wstring& page, double now);
}
