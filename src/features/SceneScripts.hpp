#pragma once
// What the level's scripts do when the character walks, read from the script text the game
// loads, without the game, so that the rules can be tested on the scripts themselves. Two kinds
// of script answer her walking. A trigger's script runs when she enters the volume
// (gEvents["EnterTrigger"]). The state machines of the scenes are run by the game at every
// update, each in one state at a time; a waiting state asks whether a character is inside a
// volume (SF_IsInTrigger, the game's UBatesGameScriptInterface::IsInTrigger) and its machine
// moves on once she is. A scene moves on when what follows starts a sequence, an activity of the
// story (not an ambient animation, PAO_), a film or the next level, or sets the objective; it
// does not when only dialogue, sound, lights, the camera or other volumes follow. A machine's
// wait also moves the scene on when it sends a companion on ahead (Act2_2's walk to the mine,
// where Jessica is led on volume by volume); a trigger's script that does so does not, since it
// runs whenever she walks in and the story never waits on it (act2_3's Emily, act2_11's Josh).

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace da::scripts
{
    // Whether a call of the scripts' library moves a scene on (a companion's order aside), and
    // whether an activity is the story's.
    bool StoryCall(std::wstring_view call);
    bool StoryActivity(std::wstring_view activity);

    // Whether starting an activity actor moves a scene on, by what the actor says of itself: an
    // activity of the background (bIsBackgroundActivity), a performance loop (bIsPAOSequence), or one
    // the scripts would not count by its name (StoryActivity) does not; one with no name to judge by
    // is weighed by its flags alone. act3_4's BP_CharacterTriggerVolume_C_7 starts PAO_rat_CaveFloor,
    // rats on the cave's floor by the mine's note: listed as a way on, it was walked to on 2026-11-07
    // and nothing came of it.
    bool StoryActor(std::wstring_view coreName, bool background, bool performance);

    // A number the scripts keep in gVariables, as their comparison (SF_Comparison of type
    // "Number", the library's Compare) sees it: nothing yet (nil), a number, or not known to the
    // mod, when a condition on it is taken either way.
    struct Number
    {
        enum class Kind
        {
            Unknown,
            Nil,
            Value,
        };
        Kind kind = Kind::Unknown;
        double value = 0.0;
    };
    using Variables = std::function<Number(const std::wstring& name)>;

    // The library's Compare(operation, value1, value2) for numbers or nil, its nil cases first;
    // no answer for an operation it does not know.
    std::optional<bool> Compare(std::wstring_view operation, const Number& a, const Number& b);
    // A condition of a state that is one comparison of numbers ("SF_Comparison(sm, {number2 = 1,
    // number1 = gVariables.bAllowLostPhone, type = "Number", operation = "Equal"})", "not" before
    // it or not), weighed with the variables now; no answer for any other condition, or one on a
    // variable not known.
    std::optional<bool> Holds(const std::wstring& condition, const Variables& variables);

    struct Script; // a trigger script's state machines, as read

    // A trigger's script: the trigger it answers, how many times (-1 every time), the calls it
    // makes and the activities it names, every condition taken either way; and the variables its
    // conditions compare.
    struct EnterEvent
    {
        std::wstring trigger;
        int limit = -1;
        std::vector<std::wstring> calls;
        std::vector<std::wstring> activities;
        bool story = false;
        std::vector<std::wstring> variables;
        std::shared_ptr<const Script> script;
    };
    bool ParseEnterEvent(const std::wstring& script, EnterEvent& out);

    // Whether walking in moves the scene on now: what the script's machine reaches from its
    // start with its comparisons weighed as the game weighs them with `variables`, the other
    // conditions either way. T_EnteredLounge starts Jessica's lost-phone talk only while
    // gVariables.bAllowLostPhone is 1 and sets it to 0 as it does; it stood listed as a way on in
    // the cabin long after the talk, said reached and never gone (2026-11-06).
    bool StoryNow(const EnterEvent& event, const Variables& variables);

    // The trigger scripts the player's character has set off, for as long as the level's scripts
    // run: a script's limit counts its runs whoever walks in (the game's own gEvents entry, by the
    // trigger's name), so a change of character keeps them. At chapter one Sam set off the totem
    // plaque's trigger, run once; the record was cleared when Chris took over, and the trigger
    // was listed again behind him (2026-11-01).
    class SetOff
    {
    public:
        // What the game's own scripts keep of a trigger's script: its entry in gEvents.EnterTrigger,
        // kept while it has runs left and taken out once they are used up (Events.lua's Event_Start
        // counts triggerLimit down and removes the trigger at 0); unknown where the scripts' state
        // cannot be read.
        enum class Entry
        {
            Unknown,
            Gone,
            Kept,
        };
        void Walked(const std::wstring& trigger)
        {
            if (!trigger.empty()) m_triggers.insert(trigger);
        }
        // The script has run as many times as it may (the record counts a first run).
        bool Spent(const EnterEvent& event) const
        {
            return event.limit > 0 && m_triggers.count(event.trigger) > 0;
        }
        // The same by the game's own record where it can be read, by her walking in where it cannot.
        // The mod's record is what its looks saw, and a trigger whose script starts its scene in the
        // frame she walks in is gone from under her before any look: act6_1's
        // T_hannahslair_roofcollapse took Emily into the roof's fall at once, and after the scene the
        // trigger, run once and spent, was offered again 15 m back, reached, and gone with nothing
        // happening (research/sessions/s59-full.log 16:00:44 to 16:01:25).
        bool Spent(const EnterEvent& event, Entry entry) const
        {
            if (entry == Entry::Gone) return true;
            if (entry == Entry::Kept) return false;
            return Spent(event);
        }
        // The game's record as it was last read stands for a read that fails (the scripts' lock held a
        // moment by another thread): the roof's trigger, spent, would otherwise turn back into a way on
        // for that look, said as news, and its going again would forget every refusal of the ground.
        Entry Known(const std::wstring& trigger, Entry entry)
        {
            if (entry != Entry::Unknown)
            {
                m_entries[trigger] = entry;
                return entry;
            }
            const auto known = m_entries.find(trigger);
            return known == m_entries.end() ? entry : known->second;
        }
        // Other scripts run: the level's script group changed.
        void NewScripts()
        {
            m_triggers.clear();
            m_entries.clear();
        }

    private:
        std::set<std::wstring> m_triggers;
        std::map<std::wstring, Entry> m_entries;
    };

    // A state's question whether a character is inside a volume, at any depth of its conditions,
    // and what follows a yes: the calls on the way to the machine's next wait. The question waits
    // for her only in a state that waits (registered with no next state) or that comes back to
    // itself (a loop through a wait for time); a state that passes on asks it once in passing (the
    // answerphone in act2_11). A yes moves the machine only when the link jumps: one that never
    // does leaves the state asking for good (a door slam's last state). A question whose way on
    // leads only to another such question for the same character moves the scene on as that one
    // does, since the other is not asked before she has walked into this one.
    struct Poll
    {
        std::wstring source;  // the script it is in
        std::wstring machine; // the state machine
        int state = 0;
        std::wstring trigger;
        std::wstring who;                     // the game object asked about: "Player" or a character's name
        bool negated = false;                 // the machine moves on once the character is not inside
        bool waits = false;                   // its state waits on it, not asking it once in passing
        bool leaves = false;                  // a yes jumps to another state
        bool story = false;                   // walking in moves the scene on
        std::vector<std::wstring> calls;      // on the way on, in order
        std::vector<std::wstring> activities; // named on the way on
        // Moves the scene on only by sending a companion on ahead (`sentOn`, by name), which counts
        // only where the level waits for that companion (WeighCompanions).
        bool sentOnly = false;
        std::vector<std::wstring> sentOn;
        // A yes ends its machine (SF_End on the way on): the scene is over, which moves the story on
        // where the level waits for it to be done (WeighEnds).
        bool ends = false;
    };

    // A machine's wait for another to be done (SF_IsStatemachineDone), and whether what follows moves
    // the story on: running the next scene's machine or the story's own calls. Act2_2's main flow runs
    // its scenes one after another, each once the one before is done; its dialogue's machine waits for
    // the last scene only to end itself.
    struct EndWait
    {
        std::wstring source;
        std::wstring machine; // the machine that waits
        int state = 0;
        std::wstring awaited; // the machine whose end it waits for
        bool story = false;
    };
    std::vector<EndWait> ParseEndWaits(const std::wstring& script, const std::wstring& source);

    // A companion leading the player on: the state of a machine whose every way in comes last through an
    // order to her to lead the player (SF_OrderLeadTo with lead = "Player"), no other order for her
    // between. A lead holds her to the player: Act2_2's "Lead_5m" (DA_BatesAIStyleData) walks Jessica up
    // to 5 m ahead, waits once Mike is 7 m behind and comes back for him at 10 m. So where a scene waits
    // for her to come into a volume, the story goes on only as the player keeps up: after Mike walks into
    // T_MineEntrance the mine's scene waits for Jessica at the pit's edge (T_EnterMineChoice), and a player
    // who walked through the entrance's strip unawares and on elsewhere had nothing listed while she
    // waited for him (Andrey's friend, 2026-11-11).
    struct Leading
    {
        std::wstring source;
        std::wstring machine;
        int state = 0;
        std::wstring character;
    };
    std::vector<Leading> ParseLeads(const std::wstring& script, const std::wstring& source);
    // The companions leading the player now, by where the machines stand (lua::Machines).
    std::set<std::wstring> LeadingNow(const std::vector<Leading>& leads, const std::vector<std::pair<std::wstring, int>>& machines);

    // A question whose yes ends its scene moves the story on where the level waits for that scene to be
    // done and goes on with the story then: Act2_2's walk to the cabin ends as Mike walks into
    // T_Jessica_Move_To_Mine_Entrance, and the main flow then runs the mine's scene. Judged nothing of
    // the story's, the volume was never a way on: after "Обойдите завал." the list stood empty until
    // Mike happened to walk through it (research/sessions/s70-play.log, 12:25:44 to 12:26:23). Weighed
    // once over every question of the level's scripts; returns how many it found to move the story on.
    size_t WeighEnds(std::vector<Poll>& polls, const std::vector<EndWait>& ends);

    // What a trigger's script turns on or off when she walks into the volume or out of it
    // (gEvents["EnterTrigger"] or gEvents["LeaveTrigger"], SF_SetEnableActivity): the trigger, whether
    // it is its leaving, and each activity with whether it is turned on.
    struct ActivitySwitch
    {
        std::wstring trigger;
        bool leave = false;
        std::wstring activity;
        bool enable = false;
    };
    std::vector<ActivitySwitch> ParseActivitySwitches(const std::wstring& script);

    // The volumes each activity is off in while she stands there: walking in turns it off and walking
    // out turns it on again. act4_2's T_disable_cigar_box turns Jack's cigar box off on the desk's
    // near side; five walks to the box went in through it, the game turned the box off a metre or two
    // short, and each walk ended saying the target was gone (2026-11-07). act4_1's scarecrow look-at
    // and act4_2's drop into the cellar are turned off so too.
    std::map<std::wstring, std::vector<std::wstring>> OffInside(const std::vector<ActivitySwitch>& switches);

    // Every question of the state machines in a script, judged. `source` names the script.
    std::vector<Poll> ParsePolls(const std::wstring& script, const std::wstring& source);

    // A poll whose way on turns the scene's objective off or sets the next (SF_TurnOffCurrentObjective,
    // SF_SetCurrentObjective): its trigger is where the objective leads (ways::Entry::goal). The lodge's
    // basement in act8_6 (Scene03_find_our_friends 3) and the hanging shed's door in act6_4
    // (Scene03_leave_hanging_shed 4) are such ways.
    bool EndsObjective(const Poll& poll);

    // A companion sent on ahead moves the scene on only where a scene of the level waits for that
    // companion to come into a volume, and that wait moves the scene on by itself: Act2_2's story
    // waits for Jessica in T_EnterMineChoice to start the mine's choice, and its companion's machine
    // sends her on volume by volume as Mike walks. act5_2's companion's machine sends Matt about the
    // radio tower as Emily walks, and no scene waits for him there: its volumes came and went from
    // the list as ways on, said reached and gone with nothing of the story behind them (2026-11-08).
    // A companion no scene of the level ever waits for is weighed here, once, over every question of
    // the level's scripts (`polls`, all of them); one a scene waits for at some time only counts while
    // that wait is asked (Waits::Judge). Returns how many questions it judged no longer to move the
    // scene on.
    size_t WeighCompanions(std::vector<Poll>& polls);

    // A state's question whether a character is moving (SF_IsCharacterMoving, the game's
    // UBatesGameScriptInterface::IsCharacterMoving) and whether walking on moves the scene on: after
    // Jessica is taken in the cabin, act2_5's Scene08_insidethecabin asks in its state 51 whether Mike
    // moves at 0.1 m/s for 0.75 s, then starts A_JessicaGrabbed. The mod handed him over with nothing
    // to walk to and he stood half a minute (2026-11-06). Other scenes wait for stillness with the
    // same call (act5_2's security light: "not moving").
    struct MoveWait
    {
        std::wstring source;
        std::wstring machine;
        int state = 0;
        std::wstring who;
        bool negated = false;
        bool waits = false;
        bool story = false;
    };
    std::vector<MoveWait> ParseMoveWaits(const std::wstring& script, const std::wstring& source);

    // Whether a scene's machine stands now in a state that waits for `character` (or the player) to
    // walk on and moves the scene on once she does, by the machines' own record of where each stands
    // (`machines`: the machine's name, its state).
    bool WaitsToMove(const std::vector<MoveWait>& waits, const std::vector<std::pair<std::wstring, int>>& machines, const std::wstring& character);

    // The numbers the level's scripts keep in gVariables ("gVariables.SceneTransitionFadeWaitTime
    // = 2", act2_1's MainFlow), by name.
    std::map<std::wstring, double> ParseVariables(const std::wstring& script);

    // A scene's switch of the player's character, and the wait for time its machine goes on to
    // before the scene goes on by itself (the wait's way on moves the story on). act2_1's snowball
    // fight makes Jessica the player, waits SceneTransitionFadeWaitTime (2 s) and then starts her
    // scene; meanwhile she stands unbound on the exploration camera, and the hold handed her over
    // for the second and a half before the scene took her (2026-11-02). `seconds` is 0 where the
    // switch is followed by no such wait.
    struct Switch
    {
        std::wstring machine;
        int state = 0;
        std::wstring character;
        double seconds = 0.0;
    };
    std::vector<Switch> ParseSwitches(const std::wstring& script, const std::map<std::wstring, double>& variables);

    // The questions the game asks as its scenes run, and what they make of the player's character
    // walking into a volume now. A volume can be asked about by two machines, one waiting to move
    // the story on and one to play a line of dialogue (act2_4's fireplace); the game is not asked
    // which machine asks. A machine stands in one state at a time, so a volume that only one
    // state of all asks about, asked now, places its machine in that state, and the machine's
    // other questions are not the ones being asked. A machine resting where it asks nothing (an
    // activity running) cannot be placed so; its questions about a volume another machine asks
    // about count as asked.
    constexpr double kAskedWithin = 1.0; // seconds of play: a waiting state asks at every update

    class Waits
    {
    public:
        // The questions of every script the level has loaded.
        void SetPolls(std::vector<Poll> polls);
        const std::vector<Poll>& Polls() const
        {
            return m_polls;
        }
        // The game asked whether a character is inside `trigger`; `player` when it asked about
        // the player's. `now` is a clock that stands still while the game is paused or loading,
        // when its scripts do not run.
        void Asked(double now, const std::wstring& trigger, bool player);
        // The game's own answer to that question about the player's character (the function's return
        // value, read as it returns): a yes is the wait met, and the machine that asked moves on.
        void Answered(double now, const std::wstring& trigger, bool yes);
        void Forget()
        {
            m_asked.clear();
        }

        enum class Kind
        {
            None,    // no state asks about the player's character there
            NoStory, // the states that ask move no scene on
            NotNow,  // a state that would move the scene on is not the one asking now
            Waiting, // the scene waits for her to walk in
            Met,     // the game has answered the scene's last question yes: she has walked in
        };
        struct Verdict
        {
            Kind kind = Kind::None;
            const Poll* poll = nullptr; // the state behind the verdict, when there is one
        };
        // What the scenes make of the player's character, named `character`, walking into
        // `trigger` at `now`. `machines`, when the game's Lua state could be read, is each running
        // machine with the state it stands in (lua::Machines): a state of a machine found there
        // waits only while its machine stands in it, whoever else asks about the volume. In act7_2
        // on 2026-11-09 the dialogue's machine (Dialogue_control 3 and 6) asked at every update
        // whether Mike stood in T_wendigo_sighting, to play a line, while the scene's own wait for it
        // (Scene03_not_alone 35) was states away (it stood in 28, 31, then 42): the asks were taken
        // for the scene's, and the volume was a way on for nine minutes
        // (research/sessions/s63-play.log, 00:19:20 to 00:28:32).
        // `leading`, the companions leading the player now (LeadingNow): a scene's wait for one of them
        // to come into the volume is the player's way on too, judged by the game's questions about her.
        Verdict Judge(double now, const std::wstring& trigger, const std::wstring& character,
                      const std::vector<std::pair<std::wstring, int>>* machines = nullptr, const std::set<std::wstring>* leading = nullptr) const;

    private:
        bool AskedNow(double now, const std::wstring& trigger, bool player) const;
        // Whether a state other than `state` of the machine is known to be the one it stands in.
        bool ElsewhereNow(double now, const Poll& poll) const;
        // Whether a scene waits now for a companion the question sends on, the player's character
        // `character` not being that companion: a wait of the story's for her in a volume, asked
        // within kAskedWithin (WeighCompanions). The deer's scene waits for Matt while he is the
        // player, and is over long before Emily walks the tower with him.
        bool CompanionAwaited(double now, const Poll& poll, const std::wstring& character) const;

        struct Asking
        {
            double any = -1000.0;
            double player = -1000.0;
            double yes = -1000.0; // the last yes the game gave about the player's character
        };
        std::vector<Poll> m_polls;
        std::map<std::wstring, Asking> m_asked;
    };

    // A volume a scene has waited on is a way on until the character walks into it (the game's own
    // yes to the scene's question, Waits::Kind::Met, or her standing in it at a look) or the story
    // moves on, though the machine that waits leaves off asking for a while: a companion's machine
    // asks only while its character is not busy with her own (Jessica's lead out of the mine on
    // 2026-11-05: Aicontroller 44 asked about T_ExitMine after the cart, left off as Mike went to
    // the totem and asked again two minutes later, four times over in fourteen minutes, each time
    // round an interaction of his, and the way out came and went from the list with it). A volume no
    // state of any scene would move on for is no way on whatever it was. A wait met between two looks
    // is over all the same: the cellar's ghost scare (2026-11-08) took Ashley as she walked into
    // T_door_ghost_scare, no look saw her in it, and the volume was held as a way on for ten minutes
    // after the scare, in and out of the list as she walked the cellar
    // (research/sessions/s58-whole.log, 11:28:01 to 11:38:35); so did the old kitchen's door slam.
    class HeldWaits
    {
    public:
        // Whether `trigger` counts as waited on at this look, from the scenes' verdict on it now and
        // whether the character stands in it.
        bool Waiting(const std::wstring& trigger, Waits::Kind kind, bool inside);
        // The story moved on (another volume set off), the objective changed, or the level went.
        void Forget()
        {
            m_held.clear();
        }
        bool Held(const std::wstring& trigger) const
        {
            return m_held.count(trigger) > 0;
        }

    private:
        std::set<std::wstring> m_held;
    };
}
