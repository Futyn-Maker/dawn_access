#pragma once
// What the speech policies decide, worked out without the reader so that it can be tested.

#include <map>
#include <set>
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
    // Each line is kept once, at its last place, and a line withdrawn since it was said (`withdrawn`:
    // when each text was last withdrawn, Withdraw: a prompt the game has taken down) is not kept: at
    // the office desk of act7_2 every quick turn kept every line behind the page before it, the copies
    // kept by the turn before among them, and the stick's prompts of pages turned past: 17 lines after
    // the sixth page (research/sessions/s63-play.log, 00:24:38 to 00:24:56).
    struct TurnPlan
    {
        bool cut = true;
        std::vector<std::wstring> kept;
        std::vector<std::wstring> dropped; // queued, never begun, and withdrawn: dropped unheard
    };
    TurnPlan Turn(const std::vector<Said>& said, const std::wstring& page, double now, const std::map<std::wstring, double>* withdrawn = nullptr);

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

    // Who holds what the game says by itself (Hold): each owner lets go of its own, and the lines
    // kept are said once the last lets go. A choice's side taken while its key still repeated holds
    // the game's lines until the key is up, and the prologue starts a quick-time event at once after
    // a choice: with one shared hold, the choice letting go would have let the game speak over the
    // event's key, which the event's hold is there to keep quiet.
    class Holders
    {
    public:
        // An owner takes hold: true when this begins the hold.
        bool Hold(const std::wstring& owner);
        // An owner lets go: true when the hold ends with it, the kept lines to be said.
        bool Release(const std::wstring& owner);
        bool Held() const
        {
            return !m_owners.empty();
        }
        // The hold ends for every owner at once (it lapsed).
        void Clear()
        {
            m_owners.clear();
        }

    private:
        std::set<std::wstring> m_owners;
    };
}
