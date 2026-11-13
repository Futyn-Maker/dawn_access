#include "features/ShootingRules.hpp"

#include "features/ExploreText.hpp"

#include "core/Strings.hpp"

#include <algorithm>
#include <cmath>
#include <cwchar>

namespace da::shoot
{
    std::wstring TargetLabel(bool foundByName, std::wstring_view actorName, std::wstring_view nextStage)
    {
        if (foundByName && !actorName.empty())
        {
            std::wstring name(actorName);
            // A skeletal actor the stage animates, as the designers prefix it.
            if (str::StartsWith(name, L"BASA_")) name.erase(0, 5);
            const auto words = explore::NameWords(name);
            if (!words.empty()) return words;
        }
        std::wstring words = explore::NameWords(nextStage);
        // The stage is named for what the hit does to the thing: the verb goes.
        for (const wchar_t* verb : {L"Shoot ", L"Hit ", L"Throw ", L"Attack ", L"Stab "})
        {
            if (words.size() > wcslen(verb) && str::StartsWith(str::ToLower(words), str::ToLower(verb)))
            {
                words.erase(0, wcslen(verb));
                break;
            }
        }
        return words;
    }

    int Lead(const std::vector<Candidate>& candidates, int chosen, int leading)
    {
        const Candidate* nearest = nullptr;
        const Candidate* kept = nullptr;
        const Candidate* on = nullptr;
        for (const auto& c : candidates)
        {
            if (!c.sight.placed) continue;
            if (chosen >= 0 && c.index != chosen) continue;
            if (c.sight.onTarget && (!on || c.sight.angle < on->sight.angle)) on = &c;
            if (c.index == leading) kept = &c;
            if (!nearest || c.sight.angle < nearest->sight.angle) nearest = &c;
        }
        if (on) return on->index;
        if (kept && nearest != kept && nearest->sight.angle > kept->sight.angle * kKeepMargin) return kept->index;
        return nearest ? nearest->index : -1;
    }

    bool FireWord::Update(double now, int led, bool solid)
    {
        if (led != m_led)
        {
            m_led = led;
            m_called = false;
            m_onSince = -1.0;
        }
        if (led < 0) return false;
        if (!solid)
        {
            m_onSince = -1.0;
            if (m_called && now - m_onAt > kFireAgain) m_called = false;
            return false;
        }
        if (m_onSince < 0.0) m_onSince = now;
        m_onAt = now;
        if (m_called || now - m_onSince < kFireAfter) return false;
        m_called = true;
        return true;
    }

    void FireWord::Reset()
    {
        *this = FireWord{};
    }

    Verdict Judge(const Ending& e)
    {
        if (e.still || e.unseen) return Verdict::None;
        // A shot counts first: in a stage that waits for its end marker after its time, a
        // highlighted shot still takes its target once the clock has run out.
        if (e.shotCircle || e.chosenChanged) return Verdict::Hit;
        if (e.aimingOnly && e.aimReached) return Verdict::AimReached;
        const bool clockOut = e.timed && e.limit > 0.0 && e.seconds >= e.limit - kClockSlack;
        if ((e.timedOutNow && !e.timedOutBefore) || clockOut) return Verdict::TimedOut;
        return Verdict::None;
    }

    DilemmaPlace DilemmaAt(double yaw, double minYaw, double maxYaw, double middle)
    {
        DilemmaPlace p;
        const double span = maxYaw - minYaw;
        if (span <= 0.0) return p;
        p.t = std::clamp((yaw - minYaw) / span, 0.0, 1.0);
        const double low = (1.0 - middle) / 2.0;
        const double high = (1.0 + middle) / 2.0;
        p.region = p.t < low ? 0 : (p.t > high ? 1 : -1);
        const double lowYaw = minYaw + low * span;
        const double highYaw = minYaw + high * span;
        p.toFirst = std::min(0.0, lowYaw - yaw);
        p.toSecond = std::max(0.0, highYaw - yaw);
        return p;
    }

    std::vector<std::wstring> DilemmaWords(const std::wstring& leftKey, const std::wstring& rightKey, const std::wstring& left, const std::wstring& right)
    {
        if (left.empty() || right.empty()) return {};
        if (leftKey.empty() || rightKey.empty()) return {left, right};
        return {leftKey, left, rightKey, right};
    }

    bool NamesHighlight(bool dilemma, bool finishOnHighlight)
    {
        return dilemma && !finishOnHighlight;
    }

    DilemmaLead LeadDilemma(const DilemmaPlace& p, int option, bool highlighted, bool invert)
    {
        DilemmaLead lead;
        const double side = invert ? -1.0 : 1.0;
        if (option == 0 || option == 1)
        {
            Sight s;
            s.placed = true;
            const double toGo = option == 0 ? p.toFirst - (p.region == 0 ? 0.0 : kDilemmaMargin) : p.toSecond + (p.region == 1 ? 0.0 : kDilemmaMargin);
            s.horizontal = p.region == option ? 0.0 : side * toGo;
            s.angle = std::fabs(s.horizontal);
            s.onTarget = p.region == option;
            lead.blip = BlipOf(s);
            lead.interval = BlipInterval(s);
            return lead;
        }
        lead.blip.pan = std::clamp(side * (2.0 * p.t - 1.0), -1.0, 1.0);
        lead.blip.level = 0.5;
        lead.blip.locked = highlighted;
        lead.interval = highlighted ? kOnTargetInterval : kDilemmaPace;
        return lead;
    }

    double BlipInterval(const Sight& s)
    {
        return s.onTarget ? kOnTargetInterval : 0.09 + 0.4 * std::clamp(s.angle / 25.0, 0.0, 1.0);
    }

    Blip BlipOf(const Sight& s)
    {
        Blip b;
        b.pan = std::clamp(s.horizontal / 12.0, -1.0, 1.0);
        b.level = 0.5 + std::clamp(s.vertical / 12.0, -0.5, 0.5);
        b.locked = s.onTarget;
        b.muffled = std::fabs(s.horizontal) > 90.0;
        return b;
    }

    bool ConditionHolds(const AimCondition& c, double yaw, double pitch)
    {
        const double v = c.axis == 0 ? pitch : yaw;
        switch (c.op)
        {
        case 0: return std::fabs(v - c.value) < 1e-3;
        case 1: return std::fabs(v - c.value) >= 1e-3;
        case 2: return v < c.value;
        case 3: return v <= c.value;
        case 4: return v > c.value;
        case 5: return v >= c.value;
        default: return false;
        }
    }

    std::wstring AimWay(const Sight& goal, bool invertX)
    {
        if (!goal.placed || goal.onTarget || goal.angle <= 0.0) return {};
        if (std::abs(goal.horizontal) >= std::abs(goal.vertical)) return (goal.horizontal > 0.0) != invertX ? L"Right" : L"Left";
        return goal.vertical > 0.0 ? L"Up" : L"Down";
    }

    Sight AimGoal(const std::vector<AimCondition>& conditions, double yaw, double pitch)
    {
        Sight s;
        double goalYaw = yaw, goalPitch = pitch;
        bool all = !conditions.empty();
        for (const auto& c : conditions)
        {
            const bool holds = ConditionHolds(c, yaw, pitch);
            all = all && holds;
            if (holds) continue;
            double& goal = c.axis == 0 ? goalPitch : goalYaw;
            if (c.op == 4 || c.op == 5)
                goal = std::max(goal, c.value + kAimGoalMargin);
            else if (c.op == 2 || c.op == 3)
                goal = std::min(goal, c.value - kAimGoalMargin);
            else if (c.op == 0)
                goal = c.value;
            else
                continue; // "not equal" names no way to turn
            s.placed = true;
        }
        s.onTarget = all;
        s.solid = all;
        if (all) s.placed = true;
        s.horizontal = goalYaw - yaw;
        s.vertical = goalPitch - pitch;
        s.angle = std::hypot(s.horizontal, s.vertical);
        return s;
    }

    namespace
    {
        constexpr double kDegrees = 180.0 / 3.14159265358979323846;

        // The engine's FRotator of a direction (FVector::ToOrientationRotator): yaw about the up axis
        // from the X axis, pitch up from the ground, in degrees.
        AimAngles Orientation(const Vec& d)
        {
            return AimAngles{std::atan2(d.y, d.x) * kDegrees, std::atan2(d.z, std::hypot(d.x, d.y)) * kDegrees};
        }

        double ShortWay(double degrees)
        {
            degrees = std::fmod(degrees, 360.0);
            if (degrees > 180.0) degrees -= 360.0;
            if (degrees <= -180.0) degrees += 360.0;
            return degrees;
        }
    }

    AimAngles AimOnto(const Vec& camera, const Vec& start, const Vec& target, bool assisted)
    {
        const auto toStart = Orientation(Vec{start.x - camera.x, start.y - camera.y, start.z - camera.z});
        const auto toTarget = Orientation(Vec{target.x - camera.x, target.y - camera.y, target.z - camera.z});
        const double yaw = toTarget.yaw - toStart.yaw;
        return AimAngles{assisted ? yaw : ShortWay(yaw), toTarget.pitch - toStart.pitch};
    }

    Vec AimDirection(const Vec& camera, const Vec& start, const AimAngles& angles)
    {
        const auto toStart = Orientation(Vec{start.x - camera.x, start.y - camera.y, start.z - camera.z});
        const double yaw = (toStart.yaw + angles.yaw) / kDegrees;
        const double pitch = (toStart.pitch + angles.pitch) / kDegrees;
        // FRotator::Vector: (cos pitch cos yaw, cos pitch sin yaw, sin pitch).
        return Vec{std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), std::sin(pitch)};
    }

    bool Snaps(const SnapStage& stage)
    {
        return stage.targets > 0 && !stage.aimingOnly && !stage.targetsHidden && !stage.finishOnHighlight && !stage.dilemma;
    }

    bool Asks(size_t targets, bool aimingOnly)
    {
        return targets > 0 || aimingOnly;
    }

    Lock LockHolds(bool stageRuns, bool targetPlaced, bool playerAims, bool aimKnown)
    {
        if (!stageRuns) return Lock::StageOver;
        if (!targetPlaced) return Lock::TargetGone;
        if (!aimKnown) return Lock::AimUnknown;
        if (playerAims) return Lock::PlayerAims;
        return Lock::Holds;
    }

    bool SnapNow(bool fireHeld)
    {
        return !fireHeld;
    }

    bool Cadence::Due(double now, bool onTarget, double interval)
    {
        const bool rose = onTarget && !m_onTarget;
        m_onTarget = onTarget;
        if (!rose && now - m_last < interval) return false;
        m_last = now;
        return true;
    }
}
