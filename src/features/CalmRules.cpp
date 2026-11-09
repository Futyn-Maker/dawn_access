#include "features/CalmRules.hpp"

#include <algorithm>
#include <cmath>

namespace da::calm
{
    StartWords StartWordsOf(bool drawn, bool said, bool modeKnown, bool passive)
    {
        if (said) return StartWords::Wait;
        if (drawn) return StartWords::Game;
        return modeKnown && !passive ? StartWords::Mod : StartWords::Wait;
    }

    bool Beyond(const Marker& m)
    {
        return std::max(std::fabs(m.x), std::fabs(m.y)) >= m.edge;
    }

    Step Follower::Update(double now, const Marker& m, bool modeKnown, bool passive)
    {
        Step step;
        const double offset = std::max(std::fabs(m.x), std::fabs(m.y));
        const bool beyond = offset >= m.edge;
        const double share = std::min(offset / m.edge, 1.0);
        step.beyond = beyond;
        bool cameBack = false;
        if (beyond != m_beyond)
        {
            m_beyond = beyond;
            step.beyondChanged = true;
            cameBack = !beyond;
        }
        // The marker back inside: the double ping, at once and before anything else.
        if (cameBack && modeKnown && !passive)
        {
            m_lastPingAt = now;
            m_lastBlipAt = now;
            step.pingBack = true;
            step.sounds.push_back({Sound::Kind::Ping});
        }
        const bool pingHeard = now - m_lastPingAt < kPingHeard;
        // The passive version judges the controller's motion, not the marker.
        const bool byMotion = modeKnown && passive;
        const bool moving = byMotion && m.instability >= kMoving;
        if (byMotion)
        {
            step.moving = moving;
            if (moving != m_moving)
            {
                m_moving = moving;
                step.movingChanged = true;
            }
        }
        // The clock: its grace spent and what the game judges out (the marker past the edge, in the
        // passive version the controller moving), the pool drains; the clock cue sounds as half and
        // three quarters of the pool are gone.
        const bool draining = m.canFail && m.graceLimit > 0.0 && m.grace >= m.graceLimit && (byMotion ? moving : beyond);
        step.draining = draining;
        if (draining != m_draining)
        {
            m_draining = draining;
            step.drainingChanged = true;
        }
        if (m.pool > 0.0 && !pingHeard)
        {
            const double used = m.clock / m.pool;
            const int poolStep = used >= 0.75 ? 2 : used >= 0.5 ? 1 : 0;
            if (poolStep > m_poolStep)
            {
                m_poolStep = poolStep;
                step.clockStep = poolStep;
                step.sounds.push_back({Sound::Kind::Clock});
            }
        }
        if (modeKnown && !passive)
        {
            // The way to push: past the edge, back towards the middle; inside, against a pull as
            // it begins or turns. Up on the screen is a smaller y.
            double wayX = 0.0;
            double wayY = 0.0;
            if (beyond)
            {
                wayX = -m.x;
                wayY = m.y;
            }
            else if (m.pulled)
            {
                wayX = -m.pullX;
                wayY = m.pullY;
            }
            const double way = std::max(std::fabs(wayX), std::fabs(wayY));
            if (way > 0.05)
            {
                const int right = static_cast<int>(std::lround(std::clamp(wayX / way, -1.0, 1.0) * 2.0));
                const int up = static_cast<int>(std::lround(std::clamp(wayY / way, -1.0, 1.0) * 2.0));
                const bool changed = right != m_steerRight || up != m_steerUp;
                if ((changed || (beyond && now - m_lastSteerAt >= kSteerApart)) && !pingHeard)
                {
                    m_steerRight = right;
                    m_steerUp = up;
                    m_lastSteerAt = now;
                    step.steered = true;
                    step.steerChanged = changed;
                    step.steerRight = right;
                    step.steerUp = up;
                    step.sounds.push_back({Sound::Kind::Steer, right / 2.0, up / 2.0, beyond});
                    return step;
                }
            }
            else if (!beyond)
            {
                m_steerRight = 0;
                m_steerUp = 0;
            }
            if (beyond) return step; // the signal is the sound past the edge
            // Inside: the double ping every half second.
            if (now - m_lastBlipAt >= kInsideTick)
            {
                m_lastBlipAt = now;
                step.sounds.push_back({Sound::Kind::Ping});
            }
            return step;
        }
        // Passive: the low thud while the game sees the controller move, the double ping while it is
        // still, each change at once.
        if (byMotion)
        {
            const double pace = moving ? kBeyondBlip : kInsideTick;
            if (!step.movingChanged && now - m_lastBlipAt < pace) return step;
            m_lastBlipAt = now;
            step.sounds.push_back({moving ? Sound::Kind::Moving : Sound::Kind::Ping});
            return step;
        }
        // Before the version is known: the blip where the marker is; the double ping when it sits in
        // the middle.
        const bool centred = offset < kCentred;
        const double interval = centred ? kInsideTick : beyond ? kBeyondBlip : kSlowestBlip - (kSlowestBlip - kFastestBlip) * share;
        if (now - m_lastBlipAt < interval) return step;
        m_lastBlipAt = now;
        if (centred)
            step.sounds.push_back({Sound::Kind::Ping});
        else
            step.sounds.push_back({Sound::Kind::Blip, std::clamp(m.x / m.edge, -1.0, 1.0), 0.5 - 0.5 * std::clamp(m.y / m.edge, -1.0, 1.0), beyond});
        return step;
    }
}
