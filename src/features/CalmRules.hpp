#pragma once
// Stay Calm (the game's "Don't Move") worked out without the game, so that it can be tested: the
// sounds that follow the marker the game draws and judges, from the game's own values.

#include <vector>

namespace da::calm
{
    // The marker as the game draws and judges it. Its place is a share of the widget's own limit
    // of travel (left to right, top to bottom); the game counts it past the edge when the larger
    // of the two shares reaches its threshold, lets it settle below another, and in the active
    // version pulls it towards a point, the pull, for a while at a time. The game's clock: a
    // grace runs while the marker is out past the limit, and after it every moment past the edge
    // drains a pool that never refills; the pool empty is the failure.
    struct Marker
    {
        double x = 0.0;
        double y = 0.0;
        bool flagWithin = true; // the game's own flag, which lags its judgement
        double edge = 0.8;
        double centre = 1.0;
        double pullX = 0.0;
        double pullY = 0.0;
        bool pulled = false;
        double clock = 0.0;
        double pool = 0.0;
        double grace = 0.0;
        double graceLimit = 0.0;
        bool canFail = false;
        // The passive version's own judgement, its widget's CurrentInstability: 0 while the controller
        // is held still, rising to 1 within a few frames as it turns faster than the version allows,
        // falling slowly once it stops; it colours the encounter's border from safe to red.
        double instability = 0.0;
    };

    // Whether the game counts the marker past the edge (its own test for its clock).
    bool Beyond(const Marker& m);

    // The words an encounter's start is told by: the game's own where it draws them (the passive
    // version's "Не двигаться!", drawn as the widget is set up, sometimes a while after it shows),
    // else, once the version is known to be the active one, the mod's short word ("Спокойно!", both
    // levels), as the active version draws none and its signals came with no word of what they were
    // (Andrey, 2026-11-09: Jessica's run in act8_5, research/sessions/s65-play.log, 14:51:07). Wait:
    // nothing yet, or nothing more once words were said.
    enum class StartWords
    {
        Wait,
        Game,
        Mod,
    };
    StartWords StartWordsOf(bool drawn, bool said, bool modeKnown, bool passive);

    // The verdict the widget's own Stage shows (WBP_DontMove): 0 its intro, 1 running, 2 failed (its
    // Tick, as the time over the threshold reaches its limit: Anim_Failed, then TriggerFailure), 3
    // succeeded (its Success function, which only BP_Success calls: Anim_Success). The game's hooks
    // tell the verdict first; this is their polled twin, which heard both of Sam's runs in the lodge
    // that the hooks did not (research/sessions/s73-play.log, 16:40:42 and 16:43:16: stage 3, the
    // widget hidden a moment later, "hidden without a verdict").
    enum class StageVerdict
    {
        None,
        Success,
        Failure,
    };
    StageVerdict VerdictOfStage(long long stage);

    // The marker's share of the way to the edge within which it counts as in the middle; the
    // pace of its blip from the middle to the edge; the pace of the double ping inside; the pace
    // of the steering signal and of the thudded blip past the edge; how long nothing else sounds
    // after the double ping.
    constexpr double kCentred = 0.15;
    constexpr double kSlowestBlip = 0.4;
    constexpr double kFastestBlip = 0.12;
    constexpr double kInsideTick = 0.5;
    constexpr double kSteerApart = 0.35;
    constexpr double kBeyondBlip = 0.2;
    constexpr double kPingHeard = 0.25;
    // The passive version counts the controller moving where its instability stands from here on:
    // the border half red.
    constexpr double kMoving = 0.5;

    // A sound of the marker's.
    struct Sound
    {
        enum class Kind
        {
            Ping,   // the double ping
            Steer,  // the signal of where to push
            Blip,   // the marker's blip where it is, before the version is known
            Clock,  // the clock cue
            Moving, // the passive version: the low thud, the game sees the controller move
        };
        Kind kind = Kind::Ping;
        double right = 0.0;  // Steer: the way to push, -1 to 1 in halves; Blip: its pan
        double up = 0.0;     // Steer: the way to push, up positive; Blip: its pitch level, 0 to 1
        bool beyond = false; // the low thud in front: the marker past the edge
    };

    // The way to push, in one word (dir.up, dir.down, dir.left, dir.right), said at both levels as each
    // new way is first signalled, while its notes go on as before (Andrey, 2026-11-10: the notes alone
    // did not tell the way soon enough at the prologue's first encounters). The word is the axis the way
    // leans on most; a way as much up as aside is up or down, as its notes are.
    enum class Way
    {
        None,
        Up,
        Down,
        Left,
        Right,
    };
    Way WayOf(int steerRight, int steerUp);

    // What a look at the marker gives: its sounds in order, and what changed, for the log.
    struct Step
    {
        std::vector<Sound> sounds;
        bool beyond = false;
        bool beyondChanged = false;
        bool pingBack = false; // the double ping for the marker back inside
        bool draining = false;
        bool drainingChanged = false;
        int clockStep = 0;         // 1 when half the pool is gone at this look, 2 three quarters
        bool steered = false;      // a signal sounds at this look
        bool steerChanged = false; // with a new way to push (else a repeat past the edge)
        int steerRight = 0;        // the way, in halves
        int steerUp = 0;
        Way word = Way::None; // the way to say now: a new way, not the word said last for this pull
        bool moving = false;  // the passive version: the game counts the controller moving
        bool movingChanged = false;
    };

    // The sounds of the marker, told apart by how many notes they have.
    //
    // Active version: the double ping means the marker is inside and nothing is asked (every half
    // second, and at once when it comes back in). A signal of two equal notes in one ear means
    // push to that side; three notes rising mean push up, falling mean push down: at once when a
    // pull begins or turns, and, past the edge, every third of a second with a low thud in front,
    // pointing back to the middle.
    //
    // Passive version: the game judges the controller's motion and nothing else (WBP_DontMove's
    // Tick: each frame it asks whether the controller turns faster than PassiveSettleStrength allows,
    // and while it does, past the grace at the encounter's start, every frame drains the pool; the
    // marker only drifts the way it turns, slowly, and plays no part). Followed by the marker, the
    // mod had said "in the middle" while the pool ran out: at Beth's encounter in the prologue's woods
    // the marker sat within 0.25 of the middle, its double ping sounding, while 1.36 of 1.50 s drained
    // and the encounter failed (research/sessions/s37-old.log, 22:00:03 to 22:00:07). So the sound is
    // the game's own judgement, as the border's colour shows it (Marker::instability): the low thud
    // every fifth of a second while the game sees the controller move, the double ping every half
    // second while it is still, each change at once.
    //
    // Before the version is known: the marker's place is the sound. A blip sounds on the side the
    // marker has drifted to, higher when it is high; past the edge a low thud comes first; the
    // double ping when it sits in the middle.
    //
    // The game's clock: the clock cue sounds as half and three quarters of the pool are gone.
    class Follower
    {
    public:
        Step Update(double now, const Marker& m, bool modeKnown, bool passive);

    private:
        bool m_beyond = false;
        bool m_moving = false;   // the passive version: the game counts the controller moving
        bool m_draining = false; // the game's clock is eating the pool
        int m_poolStep = 0;      // the shares of the pool already told
        double m_lastBlipAt = -10.0;
        double m_lastSteerAt = -10.0;
        double m_lastPingAt = -10.0;
        int m_steerRight = 0; // the way last steered, in halves
        int m_steerUp = 0;
        Way m_said = Way::None; // the way last said, until the pull ends
    };
}
