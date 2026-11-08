#pragma once
// Speech policies on top of the two voices the mod has (speech/Outputs): the player's screen
// reader, or SAPI.
//
//   Focus(text)    - for focus and navigation. Interrupts what is being said only when
//                    the player has pressed something since it started, so their own
//                    input is answered at once while the mod never cuts itself short.
//   Announce(text) - non-interrupting: the reader says it after what it is already
//                    saying (screen arrivals, prompts, notifications, subtitles).
//   Now(text)      - hard interrupt, for what the player asked for directly. No dedupe.
//   Urgent(text)   - hard interrupt for what the game gives the player a moment to answer
//                    (the button of a quick-time event): a queued word would come too late.
//   Replace(previous, text)
//                  - for text the game puts in place of what it showed a moment ago (a
//                    document's page turned): what is still being said of the old one is no
//                    longer on the screen, and is cut short; what the game said by itself
//                    after it, queued behind it, is said again before the new one. Once the
//                    reader is past the old one, the new one waits behind what it is saying
//                    (rules::Turn): a clue's card is cut neither with the page nor by it.
//   Hold()         - what the game says by itself waits while such a moment lasts:
//                    Announce lines are kept in order and said at Release(). Focus lines
//                    answer the player's own input and are never kept; Now, Urgent and
//                    Exclusive lines are said at once.
//   Exclusive(text)- non-interrupting, said even while held: the held moment's own words.
//   Stop()         - silence everything.
//   Repeat()       - speak the last utterance again.
//
// Every line is cleaned here, once: the game's markup stripped and its whitespace collapsed,
// so that no reader has to. While the mod is switched off (core/Gate) nothing is said.
//
// Thread-safe.

#include <string>
#include <string_view>

namespace da::speech
{
    void Init();

    void Focus(std::wstring_view text);
    void Announce(std::wstring_view text);
    void Now(std::wstring_view text);
    void Urgent(std::wstring_view text);
    void Replace(std::wstring_view previous, std::wstring_view text);
    // Whether the reader may still be on a page given to it (rules::StillOn).
    bool StillOn(std::wstring_view page);
    void Hold();
    void Release();
    void Exclusive(std::wstring_view text);
    void Stop();
    // Silences everything for a key the player pressed. The log notes it at Verbose only,
    // since the arrows and the D-pad come many times a minute.
    void StopForKey(std::wstring_view key);
    void Repeat();

    // Text of the last thing spoken (for the read-screen readout).
    std::wstring Last();
    // Whether the text, or an utterance containing it, was said within the given seconds:
    // the unread-text audit asks this of every text the game draws.
    bool SaidRecently(std::wstring_view text, double seconds);

    // SAPI speaks even while a screen reader runs, when so asked (the ini at the start, the
    // Speech hotkey during play).
    void SetPreferSapi(bool prefer);
    // Moves the speech between the screen reader and SAPI, says which now speaks, and keeps
    // the choice in the ini. Without a screen reader SAPI speaks either way, and that is said.
    void ToggleOutput();
    // What speaks now: the screen reader's name, or "SAPI".
    std::wstring OutputName();
}
