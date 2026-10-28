#include "features/LookTurn.hpp"

#include <cmath>
#include <numbers>

namespace da::look
{
    namespace
    {
        double Normalize(double degrees)
        {
            degrees = std::fmod(degrees, 360.0);
            if (degrees > 180.0) degrees -= 360.0;
            if (degrees < -180.0) degrees += 360.0;
            return degrees;
        }
    }

    bool CanBeLed(const Parts& p)
    {
        return !(p.byGlint && !p.explorationView && !p.onScreen);
    }

    Lead Leads(const Parts& p)
    {
        if (!CanBeLed(p)) return Lead::None;
        if (p.byGlint && !p.explorationView) return Lead::Character;
        return !p.cameraOk || (p.needCamera && p.characterOk) ? Lead::Camera : Lead::Character;
    }

    Lead Missing(const Parts& p)
    {
        if (!p.measured || !p.atHand || p.verdict || p.lookOk || (p.cameraOk && p.characterOk) || !CanBeLed(p)) return Lead::None;
        // The camera looks at it and the character does not: the character must turn. A fixed
        // shot is not the player's to turn: there she is.
        return p.cameraOk || (p.byGlint && !p.explorationView) ? Lead::Character : Lead::Camera;
    }

    void Words::Reset()
    {
        *this = Words{};
    }

    Lead Words::Update(double now, Lead missing, bool still)
    {
        if (missing != m_missing)
        {
            m_missing = missing;
            m_since = now;
        }
        if (missing == Lead::None || missing == m_said || !still) return Lead::None;
        if (m_said != Lead::None && (now - m_since < kSwitchSteady || now - m_saidAt < kWordGap)) return Lead::None;
        m_said = missing;
        m_saidAt = now;
        return missing;
    }

    void Flicker::Reset(double now)
    {
        *this = Flicker{};
        m_releasedAt = now;
    }

    void Flicker::Watch(double now, double facing)
    {
        if (m_steadySince < 0.0 || std::fabs(Normalize(facing - m_steadyYaw)) > kSteadyDegrees)
        {
            m_steadyYaw = facing;
            m_steadySince = now;
        }
    }

    void Flicker::Release(double now)
    {
        if (!m_pushing) return;
        m_pushing = false;
        m_releasedAt = now;
    }

    void Flicker::Hold(double now, double facing)
    {
        Watch(now, facing);
        m_began = false;
        Release(now);
    }

    std::optional<Push> Flicker::Update(double now, double bearing, double facing, double speed)
    {
        Watch(now, facing);
        m_began = false;
        if (m_pushing)
        {
            Release(now);
            return std::nullopt;
        }
        // The next flick waits for the last push to leave the game's input buffer, for her to
        // stand and to stop turning; a turn that never seems over is not waited for past
        // kSettleMax.
        const bool settled = speed < kStillSpeed && now - m_steadySince >= kSteady;
        const bool waited = now - m_releasedAt >= kAfterPush;
        const bool overdue = now - m_releasedAt >= kSettleMax;
        if (!(settled && waited) && !overdue) return std::nullopt;
        m_pushing = true;
        ++m_count;
        m_began = true;
        const double radians = bearing * std::numbers::pi / 180.0;
        return Push{std::cos(radians), std::sin(radians)};
    }
}
