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
        const int raise = t.inverted != t.reversed ? -1 : 1;
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

    features::world::Vec AcrossLine(const features::world::Vec& point, const features::world::Vec& from, const features::world::Vec& through,
                                    const features::world::Vec& fallback)
    {
        features::world::Vec along{through.x - from.x, through.y - from.y, through.z - from.z};
        double length = std::sqrt(along.x * along.x + along.y * along.y + along.z * along.z);
        if (length <= 1.0)
        {
            along = fallback;
            length = std::sqrt(along.x * along.x + along.y * along.y + along.z * along.z);
        }
        const features::world::Vec d{point.x - from.x, point.y - from.y, point.z - from.z};
        if (length <= 1e-9) return d;
        const features::world::Vec u{along.x / length, along.y / length, along.z / length};
        const double t = d.x * u.x + d.y * u.y + d.z * u.z;
        return features::world::Vec{d.x - u.x * t, d.y - u.y * t, d.z - u.z * t};
    }

    TotemAim TotemLead::Update(const TotemSample& s)
    {
        // How the premonition's place answers the pose: a push right that takes it further right of
        // the point the game looks at (or up, for up) leads the other way (kTotemSlope), the answer
        // weighed as a slope over the steps of the pose, once it has moved enough (kTotemEvidence).
        // Weighed only between samples measured from the same thing, the line or the point it meets
        // the totem at.
        if (s.turnKnown)
        {
            const double dx = s.turnX - m_lastX;
            const double dy = s.turnY - m_lastY;
            if (!m_have || s.onTotem != m_lastOn)
            {
                m_have = true;
            }
            else if (std::fabs(dx) >= kTotemStep || std::fabs(dy) >= kTotemStep)
            {
                m_xx = m_xx * kTotemForget + dx * dx;
                m_xLateral = m_xLateral * kTotemForget + dx * (s.lateral - m_lastLateral);
                m_yy = m_yy * kTotemForget + dy * dy;
                m_yVertical = m_yVertical * kTotemForget + dy * (s.vertical - m_lastVertical);
                const auto judge = [](double xx, double xp, int& way)
                {
                    if (xx < kTotemEvidence) return;
                    const double slope = xp / xx;
                    if (slope > kTotemSlope)
                        way = -1;
                    else if (slope < -kTotemSlope)
                        way = 1;
                };
                judge(m_xx, m_xLateral, m_wayX);
                judge(m_yy, m_yVertical, m_wayY);
            }
            else
            {
                return Aim(s);
            }
            m_lastOn = s.onTotem;
            m_lastX = s.turnX;
            m_lastY = s.turnY;
            m_lastLateral = s.lateral;
            m_lastVertical = s.vertical;
        }
        return Aim(s);
    }

    TotemAim TotemLead::Aim(const TotemSample& s) const
    {
        TotemAim aim;
        // The game wants the point within a centimetre or two, and a straight scale left the blip near
        // the middle a few centimetres off: 3 cm across panned 0.2, 4 cm down pitched 0.41, and in s55
        // the stick was pushed up into the totem's limit for 3 s against it
        // (research/sessions/s55-full.log, 23:05:04). The scale is the square root of the offset's share.
        const auto curve = [](double value, double span) { return std::copysign(std::min(1.0, std::sqrt(std::fabs(value) / span)), value); };
        // Round the side or the back the point lies deeper than the one under the view; the blip
        // then stays full on the side the shorter way round goes.
        double pan = curve(s.lateral, 15.0);
        if (s.depth > 5.0) pan = (s.lateral < 0.0 ? -1.0 : 1.0) * std::max(std::fabs(pan), std::clamp(s.depth / 10.0, 0.0, 1.0));
        aim.pan = m_wayX * pan;
        aim.level = 0.5 + m_wayY * curve(s.vertical, 20.0) * 0.5;
        aim.behind = s.depth > 10.0 && std::fabs(s.lateral) < 4.0;
        return aim;
    }

    bool SayLookKey(bool gameAsksLook, bool keyKnown)
    {
        return !gameAsksLook && keyKnown;
    }

    Verdict FramingVerdict(const std::vector<std::wstring>& states)
    {
        for (const auto& state : states)
        {
            if (state == L"Well") return Verdict::Well;
            if (state == L"Badly") return Verdict::Badly;
        }
        return Verdict::None;
    }
}
