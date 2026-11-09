#include "features/LookTurn.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

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
        return !(p.byGlint && !p.explorationView && !p.framingEndsOnLook && !p.onScreen);
    }

    Lead Leads(const Parts& p)
    {
        if (!CanBeLed(p)) return Lead::None;
        if (p.byGlint && !p.explorationView && !p.framingEndsOnLook) return Lead::Character;
        return !p.cameraOk || (p.needCamera && p.characterOk) ? Lead::Camera : Lead::Character;
    }

    bool GlintMissing(const Parts& p, bool insideEllipse)
    {
        return p.byGlint && p.explorationView && p.measured && p.atHand && !p.verdict && insideEllipse && !p.onScreen;
    }

    Lead Missing(const Parts& p)
    {
        if (!p.measured || !p.atHand || p.verdict || p.lookOk || (p.cameraOk && p.characterOk) || !CanBeLed(p)) return Lead::None;
        // The camera looks at it and the character does not: the character must turn. A fixed
        // shot is not the player's to turn: there she is.
        return p.cameraOk || (p.byGlint && !p.explorationView && !p.framingEndsOnLook) ? Lead::Character : Lead::Camera;
    }

    bool Arrived(const Parts& p, bool autoStart, bool still, bool before)
    {
        if (p.verdict || autoStart) return true;
        if (!p.atHand || !CanBeLed(p)) return false;
        return still || before;
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

    std::optional<Push> Flicker::Update(double now, double bearing, double facing, double speed, const Vec& here)
    {
        Watch(now, facing);
        m_began = false;
        if (m_pushing)
        {
            // A held push stays on until its time is up; a flick's is let go at once.
            if (now - m_pushedAt < m_push.hold) return m_push;
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
        // What the last push did: nothing, or a step or a turn.
        if (m_count > 0)
        {
            const bool moved = std::hypot(here.x - m_flickedFrom.x, here.y - m_flickedFrom.y) >= kDeadMove;
            const bool turned = std::fabs(Normalize(facing - m_flickedFacing)) >= kDeadTurn;
            m_dead = moved || turned ? 0 : m_dead + 1;
        }
        m_pushing = true;
        ++m_count;
        m_began = true;
        m_pushedAt = now;
        m_flickedFrom = here;
        m_flickedFacing = facing;
        const double radians = bearing * std::numbers::pi / 180.0;
        m_push = Push{std::cos(radians), std::sin(radians), m_dead >= kDeadFlicks ? kPulse : 0.0};
        return m_push;
    }

    bool PressTaken(bool atHand, bool lookAsked, bool needGlint, bool glint, int64_t layer, bool measured, bool lookOk)
    {
        if (!atHand) return false;
        if (!lookAsked || !needGlint) return true;
        if (!glint) return false;
        if (layer != 1 && layer != 4) return true;
        return measured && lookOk;
    }

    bool StretchKeeps(bool offered, bool begun, double fromSpot, double wayLeft, double letGoAt)
    {
        return offered || ((begun || wayLeft < letGoAt) && fromSpot <= kStretchReach);
    }

    double LetGoAt(double slide, bool lastLeg, std::optional<double> headingOff)
    {
        if (!lastLeg || !headingOff || std::fabs(*headingOff) > kSlideAlong) return kStepWay;
        return std::max(kStepWay, slide);
    }

    void Slide::LetGo(double now, const Vec& here, double speed)
    {
        m_open = speed >= kSlideMinSpeed;
        m_from = here;
        m_speed = speed;
        m_at = now;
    }

    bool Slide::Update(double now, const Vec& here, bool still)
    {
        if (!m_open) return false;
        if (now - m_at > kSlideWithin)
        {
            m_open = false;
            return false;
        }
        if (!still) return false;
        m_open = false;
        m_seconds.push_back(std::hypot(here.x - m_from.x, here.y - m_from.y) / m_speed);
        while (m_seconds.size() > kSlideKept)
            m_seconds.pop_front();
        return true;
    }

    double Slide::Distance(double speed) const
    {
        if (m_seconds.empty() || speed <= 0.0) return 0.0;
        std::vector<double> sorted(m_seconds.begin(), m_seconds.end());
        std::sort(sorted.begin(), sorted.end());
        return std::min(sorted[sorted.size() / 2] * speed, kStretchReach);
    }

    Vec StepSpot(const std::optional<Vec>& mapped, const std::optional<Vec>& felt, const Vec& spot)
    {
        if (mapped) return *mapped;
        if (felt) return *felt;
        return spot;
    }

    bool FreshSpotMap(double mapOriginDistance)
    {
        return mapOriginDistance > kStretchReach;
    }

    BeamLead LeadAlongBeam(double facing, double stickX, double stickY)
    {
        BeamLead lead;
        const double radians = facing * std::numbers::pi / 180.0;
        lead.pan = std::clamp(std::sin(radians), -1.0, 1.0);
        lead.muffled = std::cos(radians) < 0.0;
        if (std::hypot(stickX, stickY) > kBeamPush)
        {
            const double stick = std::atan2(stickX, stickY) * 180.0 / std::numbers::pi;
            lead.locked = std::fabs(Normalize(stick - facing)) <= kBeamCone;
        }
        return lead;
    }

    void Motion::Update(double now, const Vec& here)
    {
        if (!m_samples.empty() && now < m_samples.back().first) m_samples.clear(); // another clock
        m_samples.emplace_back(now, here);
        while (m_samples.size() > 2 && now - m_samples[1].first > 2.0)
            m_samples.pop_front();
    }

    double Motion::MovedWithin(double now, double seconds) const
    {
        if (m_samples.empty()) return 0.0;
        // The farthest she has been, within the window, from where she stands last.
        const Vec& last = m_samples.back().second;
        double far = 0.0;
        for (auto it = m_samples.rbegin(); it != m_samples.rend(); ++it)
        {
            if (now - it->first > seconds) break;
            far = std::max(far, std::hypot(it->second.x - last.x, it->second.y - last.y));
        }
        return far;
    }

    double Motion::Speed(double now) const
    {
        if (m_samples.size() < 2) return 0.0;
        const auto& last = m_samples.back();
        const std::pair<double, Vec>* first = nullptr;
        for (auto it = m_samples.rbegin(); it != m_samples.rend(); ++it)
        {
            if (now - it->first > kStillWindow) break;
            first = &*it;
        }
        if (!first || last.first - first->first < 0.05) return 0.0;
        return std::hypot(last.second.x - first->second.x, last.second.y - first->second.y) / (last.first - first->first);
    }

    std::optional<double> Motion::Heading(double now) const
    {
        if (m_samples.size() < 2) return std::nullopt;
        const auto& last = m_samples.back();
        const std::pair<double, Vec>* first = nullptr;
        for (auto it = m_samples.rbegin(); it != m_samples.rend(); ++it)
        {
            if (now - it->first > kMovingWindow) break;
            first = &*it;
        }
        if (!first) return std::nullopt;
        const double dx = last.second.x - first->second.x;
        const double dy = last.second.y - first->second.y;
        if (std::hypot(dx, dy) < kMovingCm) return std::nullopt;
        return std::atan2(dy, dx) * 180.0 / std::numbers::pi;
    }

    bool Motion::Still(double now) const
    {
        if (m_samples.empty() || now - m_samples.front().first < kStillWindow) return false;
        return MovedWithin(now, kStillWindow) < kStillCm;
    }

    bool Rest::Update(double now, bool still)
    {
        if (m_readySince < 0.0) m_readySince = now;
        if (!still)
        {
            m_stillSince = -1.0;
            return now - m_readySince >= kRestMax;
        }
        if (m_stillSince < 0.0) m_stillSince = now;
        return now - m_stillSince >= kRestFor;
    }

    LookBlip LookSound(double across, double rise, bool lookedAt)
    {
        LookBlip b;
        b.pan = std::clamp(across / kLookPanSpan, -1.0, 1.0);
        b.level = std::clamp(0.5 + 0.5 * rise / kLookRiseSpan, 0.0, 1.0);
        const double off = std::hypot(across, rise);
        b.interval = lookedAt ? 0.12 : 0.09 + 0.4 * std::clamp(off / kLookPaceSpan, 0.0, 1.0);
        b.locked = lookedAt;
        b.muffled = std::fabs(across) > 90.0;
        return b;
    }
}
