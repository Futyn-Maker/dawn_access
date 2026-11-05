#include "features/NudgeRules.hpp"

#include <algorithm>
#include <cmath>

namespace da::nudge
{
    bool Valid(const Box& b)
    {
        if (b.yawMin > b.yawMax || b.pitchMin > b.pitchMax) return false;
        if (b.yawMin == 0.0 && b.yawMax == 0.0 && b.pitchMin == 0.0 && b.pitchMax == 0.0) return false;
        // The shares run from -1 to 1: a box wholly outside them can never be met.
        return b.yawMax >= -1.0 && b.yawMin <= 1.0 && b.pitchMax >= -1.0 && b.pitchMin <= 1.0;
    }

    bool Inside(const View& v, const Box& b)
    {
        return v.yaw >= b.yawMin && v.yaw <= b.yawMax && v.pitch >= b.pitchMin && v.pitch <= b.pitchMax;
    }

    Lead Toward(const View& v, const Box& b)
    {
        Lead l;
        // CameraYaw +1 is the view turned fully left: a middle at a larger share lies to the left.
        l.right = v.yaw - (b.yawMin + b.yawMax) / 2.0;
        l.up = (b.pitchMin + b.pitchMax) / 2.0 - v.pitch;
        l.sight.placed = true;
        l.sight.horizontal = l.right * v.maxYaw;
        l.sight.vertical = l.up * v.maxPitch;
        l.sight.angle = std::hypot(l.sight.horizontal, l.sight.vertical);
        l.sight.onTarget = Inside(v, b);
        l.sight.solid = l.sight.onTarget;
        l.turnRight = v.yaw > b.yawMax ? 1 : (v.yaw < b.yawMin ? -1 : 0);
        l.turnUp = v.pitch < b.pitchMin ? 1 : (v.pitch > b.pitchMax ? -1 : 0);
        return l;
    }

    shoot::Blip BlipOf(const Lead& l)
    {
        shoot::Blip blip;
        // A share of the reach either way is the whole of the ear, half a share the whole pitch
        // step, so that a framing a degree or two off is plainly to one side and above or below.
        blip.pan = std::clamp(l.right, -1.0, 1.0);
        blip.level = 0.5 + std::clamp(l.up, -0.5, 0.5);
        blip.locked = l.sight.onTarget;
        blip.muffled = false;
        return blip;
    }

    double BlipInterval(const Lead& l)
    {
        if (l.sight.onTarget) return shoot::kOnTargetInterval;
        return 0.09 + 0.4 * std::clamp(std::hypot(l.right, l.up), 0.0, 1.0);
    }

    TurnLead Toward(const Turn& t)
    {
        TurnLead l;
        const int raise = t.inverted ? -1 : 1;
        (t.vertical ? l.up : l.right) = raise;
        l.remaining = std::max(0.0, t.threshold - t.share);
        // UBatesInteractionStageExamine::UpdateMovementInput: revealing once the share is at the
        // threshold or past it.
        l.reached = t.share >= t.threshold;
        return l;
    }

    shoot::Blip BlipOf(const TurnLead& l)
    {
        shoot::Blip blip;
        // The whole ear while half the turn or more is left, never nearer the middle than 0.4, so
        // that the side to push is plain to the last.
        const double side = std::clamp(l.remaining * 2.0, 0.4, 1.0);
        blip.pan = l.right * side;
        blip.level = 0.5 + 0.5 * l.up * side;
        blip.locked = l.reached;
        blip.muffled = false;
        return blip;
    }

    double BlipInterval(const TurnLead& l)
    {
        if (l.reached) return shoot::kOnTargetInterval;
        return 0.09 + 0.4 * std::clamp(l.remaining, 0.0, 1.0);
    }

    int PercentLeft(const Turn& t)
    {
        if (t.share >= t.threshold || t.threshold <= 0.0) return 0;
        const long percent = std::lround(100.0 * (t.threshold - t.share) / t.threshold);
        return static_cast<int>(std::clamp(percent, 1L, 100L));
    }

    TurnCourse::Event TurnCourse::Update(const TurnState& s)
    {
        const bool inHand = s.usesReveal && s.graphReadsReveal && s.fullyHeld && !s.ramping;
        if (!m_revealed && inHand && s.revealing)
        {
            m_revealed = true;
            m_led = false;
            return Event::Reveals;
        }
        const bool led = !m_revealed && inHand && !s.revealing;
        if (led == m_led) return Event::None;
        m_led = led;
        return led ? Event::Begins : Event::Ends;
    }
}
