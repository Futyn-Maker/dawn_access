// features/CalmRules: the sounds that follow Stay Calm's marker, at about the pace the feature
// polls it (every third frame), in eighths of a second so that the clock adds up exactly.
#include "Support.hpp"
#include "features/CalmRules.hpp"

#include <algorithm>
#include <vector>

using namespace da;
using calm::Sound;

namespace
{
    constexpr double kPoll = 0.125;

    double T(int poll)
    {
        return poll * kPoll;
    }

    calm::Marker At(double x, double y)
    {
        calm::Marker m;
        m.x = x;
        m.y = y;
        m.edge = 0.8;
        m.centre = 0.2;
        return m;
    }

    std::vector<Sound::Kind> Kinds(const calm::Step& step)
    {
        std::vector<Sound::Kind> kinds;
        for (const auto& s : step.sounds)
            kinds.push_back(s.kind);
        return kinds;
    }

    // The times a kind of sound came at over the polls [from, to) of one marker.
    std::vector<double> Times(calm::Follower& f, const calm::Marker& m, bool modeKnown, bool passive, int from, int to, Sound::Kind kind)
    {
        std::vector<double> times;
        for (int poll = from; poll < to; ++poll)
        {
            for (const auto& s : f.Update(T(poll), m, modeKnown, passive).sounds)
            {
                if (s.kind == kind) times.push_back(T(poll));
            }
        }
        return times;
    }
}

TEST_CASE("calm: the active version inside: the double ping every half second, nothing else")
{
    calm::Follower f;
    std::vector<double> pings;
    for (int poll = 0; poll <= 16; ++poll)
    {
        const auto step = f.Update(T(poll), At(0.1, 0.0), true, false);
        for (const auto& s : step.sounds)
        {
            CHECK(s.kind == Sound::Kind::Ping);
            pings.push_back(T(poll));
        }
    }
    CHECK(pings == std::vector<double>{0.0, 0.5, 1.0, 1.5, 2.0});
}

TEST_CASE("calm: a pull is answered once as it begins and again as it turns, against it")
{
    calm::Follower f;
    f.Update(T(0), At(0.0, 0.0), true, false);
    calm::Marker pulled = At(0.1, 0.0);
    pulled.pulled = true;
    pulled.pullX = 0.5; // pulled to the right: push left
    auto step = f.Update(T(1), pulled, true, false);
    REQUIRE(step.steered);
    CHECK(step.steerChanged);
    REQUIRE(step.sounds.size() == 1);
    CHECK(step.sounds[0].kind == Sound::Kind::Steer);
    CHECK(step.sounds[0].right == doctest::Approx(-1.0));
    CHECK(step.sounds[0].up == doctest::Approx(0.0));
    CHECK_FALSE(step.sounds[0].beyond);
    // The same pull: no signal again, while the pings go on.
    for (int poll = 2; poll < 8; ++poll)
        CHECK_FALSE(f.Update(T(poll), pulled, true, false).steered);
    // The pull turns up and to the right (up on the screen is a smaller y): push down and left.
    pulled.pullY = -0.5;
    step = f.Update(T(8), pulled, true, false);
    REQUIRE(step.steered);
    CHECK(step.steerRight == -2);
    CHECK(step.steerUp == -2);
}

TEST_CASE("calm: past the edge the signal points back with its thud every third of a second, and no ping")
{
    calm::Follower f;
    f.Update(T(0), At(0.0, 0.0), true, false);
    const calm::Marker out = At(0.9, 0.0); // out to the right: push left
    auto step = f.Update(T(1), out, true, false);
    CHECK(step.beyondChanged);
    CHECK(step.beyond);
    REQUIRE(step.steered);
    CHECK(step.sounds.back().beyond);
    CHECK(step.sounds.back().right == doctest::Approx(-1.0));
    std::vector<double> signals{T(1)};
    for (int poll = 2; poll < 12; ++poll)
    {
        for (const auto& sound : f.Update(T(poll), out, true, false).sounds)
        {
            CHECK(sound.kind != Sound::Kind::Ping);
            if (sound.kind == Sound::Kind::Steer) signals.push_back(T(poll));
        }
    }
    CHECK(signals == std::vector<double>{0.125, 0.5, 0.875, 1.25});
}

TEST_CASE("calm: back inside, the double ping at once, and nothing else for a quarter of a second")
{
    calm::Follower f;
    f.Update(T(0), At(0.9, 0.0), true, false); // out to the right: the last signal said push left
    calm::Marker back = At(0.3, 0.0);
    back.pulled = true;
    back.pullX = -0.5; // a pull to the left, push right: signalled, but not over the ping
    auto step = f.Update(T(1), back, true, false);
    CHECK(step.beyondChanged);
    CHECK(step.pingBack);
    CHECK(Kinds(step) == std::vector<Sound::Kind>{Sound::Kind::Ping});
    step = f.Update(T(2), back, true, false);
    CHECK(step.sounds.empty());
    step = f.Update(T(3), back, true, false);
    REQUIRE(step.steered);
    CHECK(step.steerRight == 2);
}

TEST_CASE("calm: a pull that ends and comes again the same way is signalled again")
{
    calm::Follower f;
    f.Update(T(0), At(0.0, 0.0), true, false);
    calm::Marker pulled = At(0.1, 0.0);
    pulled.pulled = true;
    pulled.pullX = 0.5; // pulled to the right: push left
    CHECK(f.Update(T(1), pulled, true, false).steered);
    calm::Marker free = pulled;
    free.pulled = false; // the game keeps the last pull's point, no longer pulling
    CHECK_FALSE(f.Update(T(2), free, true, false).steered);
    const auto step = f.Update(T(3), pulled, true, false);
    REQUIRE(step.steered);
    CHECK(step.steerChanged);
    CHECK(step.steerRight == -2);
}

TEST_CASE("calm: no ping for the marker back inside in the passive version, nor before the version is known")
{
    for (const bool modeKnown : {true, false})
    {
        CAPTURE(modeKnown);
        const bool passive = modeKnown;
        calm::Follower f;
        f.Update(T(0), At(-0.9, 0.0), modeKnown, passive);
        const auto step = f.Update(T(1), At(0.05, 0.0), modeKnown, passive);
        CHECK(step.beyondChanged);
        CHECK_FALSE(step.pingBack);
        // The ping in the middle keeps its own half second from the last blip.
        CHECK(step.sounds.empty());
    }
}

TEST_CASE("calm: the clock cue as half and three quarters of the pool are gone, once each")
{
    calm::Follower f;
    calm::Marker m = At(0.9, 0.0);
    m.canFail = true;
    m.pool = 2.0;
    m.graceLimit = 0.65;
    m.grace = 0.7;
    auto step = f.Update(T(0), m, true, false);
    CHECK(step.draining);
    CHECK(step.drainingChanged);
    CHECK(step.clockStep == 0);
    m.clock = 1.0;
    step = f.Update(T(1), m, true, false);
    CHECK(step.clockStep == 1);
    CHECK(Kinds(step) == std::vector<Sound::Kind>{Sound::Kind::Clock});
    step = f.Update(T(2), m, true, false);
    CHECK(step.clockStep == 0);
    m.clock = 1.5;
    step = f.Update(T(3), m, true, false);
    CHECK(step.clockStep == 2);
    m.clock = 1.9;
    step = f.Update(T(4), m, true, false);
    CHECK(step.clockStep == 0);
    // Within the grace the clock does not drain.
    calm::Follower g;
    calm::Marker grace = m;
    grace.grace = 0.3;
    CHECK_FALSE(g.Update(T(0), grace, true, false).draining);
}

TEST_CASE("calm: back inside as half the pool goes, the clock cue waits for the double ping to be heard")
{
    calm::Follower f;
    calm::Marker m = At(0.9, 0.0);
    m.canFail = true;
    m.pool = 2.0;
    m.graceLimit = 0.65;
    m.grace = 0.7;
    m.clock = 0.9;
    f.Update(T(0), m, true, false);
    // Between two looks the pool passes its half and the marker comes back inside.
    m.x = 0.3;
    m.clock = 1.1;
    auto step = f.Update(T(1), m, true, false);
    CHECK(step.pingBack);
    CHECK(Kinds(step) == std::vector<Sound::Kind>{Sound::Kind::Ping});
    CHECK(f.Update(T(2), m, true, false).sounds.empty());
    step = f.Update(T(3), m, true, false);
    CHECK(step.clockStep == 1);
    CHECK(Kinds(step) == std::vector<Sound::Kind>{Sound::Kind::Clock});
}

TEST_CASE("calm: the passive version follows the game's judgement of the controller's motion, not the marker, 2026-11-08")
{
    // Still (the border safe) with the marker drifted off the middle: the double ping every half
    // second, no blip where the marker is: the game counts nothing.
    calm::Follower still;
    calm::Marker drifted = At(0.4, -0.4);
    CHECK(Times(still, drifted, true, true, 0, 9, Sound::Kind::Ping) == std::vector<double>{0.0, 0.5, 1.0});
    calm::Follower still2;
    CHECK(Times(still2, drifted, true, true, 0, 9, Sound::Kind::Blip).empty());
    // Moving (the border red) with the marker still in the middle: the low thud every fifth of a
    // second (at the polls' eighths: 0, 0.25, 0.5, ...), no ping.
    calm::Follower moving;
    calm::Marker shaken = At(0.05, 0.05);
    shaken.instability = 0.9;
    CHECK(Times(moving, shaken, true, true, 0, 9, Sound::Kind::Moving) == std::vector<double>{0.0, 0.25, 0.5, 0.75, 1.0});
    calm::Follower moving2;
    CHECK(Times(moving2, shaken, true, true, 0, 9, Sound::Kind::Ping).empty());
    // A change is sounded at once: still to moving an eighth after a ping, and back.
    calm::Follower turns;
    CHECK(Kinds(turns.Update(T(0), drifted, true, true)) == std::vector<Sound::Kind>{Sound::Kind::Ping});
    auto step = turns.Update(T(1), shaken, true, true);
    CHECK(step.movingChanged);
    CHECK(step.moving);
    CHECK(Kinds(step) == std::vector<Sound::Kind>{Sound::Kind::Moving});
    step = turns.Update(T(2), drifted, true, true);
    CHECK(step.movingChanged);
    CHECK_FALSE(step.moving);
    CHECK(Kinds(step) == std::vector<Sound::Kind>{Sound::Kind::Ping});
    // The border just under half red counts as still.
    calm::Marker settling = drifted;
    settling.instability = calm::kMoving - 0.01;
    CHECK_FALSE(turns.Update(T(3), settling, true, true).moving);
}

TEST_CASE("calm: the passive version's pool drains while the controller moves, the marker in the middle: Beth's encounter in the prologue")
{
    // research/sessions/s37-old.log, 22:00:04.337 to 22:00:07.315: the passive version, a pool of
    // 1.50 s after a grace of 0.50 s; the marker never past 0.50 of its limit while the pool ran out
    // and the encounter failed. Between each two of the mod's looks the pool grew: the game counted
    // the controller moving there (its instability up). The mod had pinged and blipped the marker in
    // the middle.
    struct Look
    {
        double at, x, y, clock, grace;
    };
    const std::vector<Look> looks{{4.337, -0.16, 0.07, 0.00, 0.11}, {4.880, -0.07, 0.08, 0.03, 0.50}, {5.418, -0.12, 0.05, 0.34, 0.50},
                                  {5.954, -0.12, 0.12, 0.67, 0.50}, {6.488, -0.25, 0.17, 0.99, 0.50}, {7.029, -0.50, 0.01, 1.36, 0.50}};
    calm::Follower f;
    double clockBefore = 0.0;
    int thuds = 0;
    std::vector<int> clockSteps;
    for (const auto& look : looks)
    {
        calm::Marker m = At(look.x, look.y);
        m.canFail = true;
        m.pool = 1.5;
        m.graceLimit = 0.5;
        m.grace = look.grace;
        m.clock = look.clock;
        m.instability = look.clock > clockBefore + 0.01 ? 1.0 : 0.0;
        clockBefore = look.clock;
        const auto step = f.Update(look.at, m, true, true);
        CAPTURE(look.at);
        if (m.instability > 0.0)
        {
            CHECK(step.moving);
            for (const auto& sound : step.sounds)
                CHECK(sound.kind != Sound::Kind::Ping);
            thuds += static_cast<int>(std::count_if(step.sounds.begin(), step.sounds.end(), [](const Sound& sound) { return sound.kind == Sound::Kind::Moving; }));
            CHECK(step.draining == (look.grace >= 0.5));
        }
        if (step.clockStep > 0) clockSteps.push_back(step.clockStep);
    }
    CHECK(thuds == 5);
    // The clock cue as half and three quarters of the pool went.
    CHECK(clockSteps == std::vector<int>{1, 2});
}

TEST_CASE("calm: before the version is known, the marker is followed as in the passive version")
{
    calm::Follower f;
    const auto step = f.Update(T(0), At(0.4, 0.0), false, false);
    REQUIRE(step.sounds.size() == 1);
    CHECK(step.sounds[0].kind == Sound::Kind::Blip);
    CHECK_FALSE(step.steered);
}

TEST_CASE("calm: the active version's start is told by the mod's word, the passive one's by the game's: Jessica's run, 2026-11-09")
{
    using calm::StartWords;
    // The active version draws no words: once it is known, the mod's word, and only once.
    CHECK(calm::StartWordsOf(false, false, true, false) == StartWords::Mod);
    CHECK(calm::StartWordsOf(false, true, true, false) == StartWords::Wait);
    // Before the version is known nothing is said: the passive one's label may yet come.
    CHECK(calm::StartWordsOf(false, false, false, false) == StartWords::Wait);
    // The passive version waits for the game's label ("Не двигаться!"), and says it as it comes.
    CHECK(calm::StartWordsOf(false, false, true, true) == StartWords::Wait);
    CHECK(calm::StartWordsOf(true, false, true, true) == StartWords::Game);
    // Words the game draws are its own, whatever the version.
    CHECK(calm::StartWordsOf(true, false, true, false) == StartWords::Game);
    CHECK(calm::StartWordsOf(true, false, false, false) == StartWords::Game);
    CHECK(calm::StartWordsOf(true, true, true, true) == StartWords::Wait);
}
