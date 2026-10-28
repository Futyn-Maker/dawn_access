// features/CalmRules: the sounds that follow Stay Calm's marker, at about the pace the feature
// polls it (every third frame), in eighths of a second so that the clock adds up exactly.
#include "Support.hpp"
#include "features/CalmRules.hpp"

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

TEST_CASE("calm: the passive version: the blip where the marker is, quicker toward the edge, the ping in the middle")
{
    // In the middle: the double ping every half second.
    calm::Follower centred;
    CHECK(Times(centred, At(0.05, 0.05), true, true, 0, 9, Sound::Kind::Ping) == std::vector<double>{0.0, 0.5, 1.0});
    // Halfway to the edge: a blip on its side, by its height, every 0.26 s.
    calm::Follower half;
    const auto step = half.Update(T(0), At(0.4, -0.4), true, true);
    REQUIRE(step.sounds.size() == 1);
    CHECK(step.sounds[0].kind == Sound::Kind::Blip);
    CHECK(step.sounds[0].right == doctest::Approx(0.5));
    CHECK(step.sounds[0].up == doctest::Approx(0.75));
    CHECK_FALSE(step.sounds[0].beyond);
    CHECK(half.Update(T(2), At(0.4, -0.4), true, true).sounds.empty());
    CHECK(half.Update(T(3), At(0.4, -0.4), true, true).sounds.size() == 1);
    // Past the edge: the thudded blip every fifth of a second.
    calm::Follower out;
    CHECK(Times(out, At(-0.9, 0.0), true, true, 0, 9, Sound::Kind::Blip) == std::vector<double>{0.0, 0.25, 0.5, 0.75, 1.0});
    const auto thud = out.Update(1.25, At(-0.9, 0.0), true, true);
    REQUIRE(thud.sounds.size() == 1);
    CHECK(thud.sounds[0].beyond);
    CHECK(thud.sounds[0].right == doctest::Approx(-1.0));
}

TEST_CASE("calm: before the version is known, the marker is followed as in the passive version")
{
    calm::Follower f;
    const auto step = f.Update(T(0), At(0.4, 0.0), false, false);
    REQUIRE(step.sounds.size() == 1);
    CHECK(step.sounds[0].kind == Sound::Kind::Blip);
    CHECK_FALSE(step.steered);
}
