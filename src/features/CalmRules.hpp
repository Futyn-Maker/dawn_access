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
    };

    // Whether the game counts the marker past the edge (its own test for its clock).
    bool Beyond(const Marker& m);

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

    // A sound of the marker's.
    struct Sound
    {
        enum class Kind
        {
            Ping,  // the double ping
            Steer, // the signal of where to push
            Blip,  // the passive version's blip where the marker is
            Clock, // the clock cue
        };
        Kind kind = Kind::Ping;
        double right = 0.0;  // Steer: the way to push, -1 to 1 in halves; Blip: its pan
        double up = 0.0;     // Steer: the way to push, up positive; Blip: its pitch level, 0 to 1
        bool beyond = false; // the low thud in front: the marker past the edge
    };

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
    };

    // The sounds of the marker, told apart by how many notes they have.
    //
    // Active version: the double ping means the marker is inside and nothing is asked (every half
    // second, and at once when it comes back in). A signal of two equal notes in one ear means
    // push to that side; three notes rising mean push up, falling mean push down: at once when a
    // pull begins or turns, and, past the edge, every third of a second with a low thud in front,
    // pointing back to the middle.
    //
    // Passive version (and while the version is not known yet): the marker's place is the sound.
    // A blip sounds on the side the marker has drifted to, higher when it is high; past the edge a
    // low thud comes first; the double ping when it sits in the middle.
    //
    // The game's clock: the clock cue sounds as half and three quarters of the pool are gone.
    class Follower
    {
    public:
        Step Update(double now, const Marker& m, bool modeKnown, bool passive);

    private:
        bool m_beyond = false;
        bool m_draining = false; // the game's clock is eating the pool
        int m_poolStep = 0;      // the shares of the pool already told
        double m_lastBlipAt = -10.0;
        double m_lastSteerAt = -10.0;
        double m_lastPingAt = -10.0;
        int m_steerRight = 0; // the way last steered, in halves
        int m_steerUp = 0;
    };
}
