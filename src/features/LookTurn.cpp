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
        // A glint the game has not put up for her comes with no turn: she must step into its volume.
        if (p.byGlint && p.glintAway) return false;
        return !(p.byGlint && !p.explorationView && !p.framingEndsOnLook && !p.onScreen);
    }

    Lead Leads(const Parts& p)
    {
        if (!CanBeLed(p)) return Lead::None;
        if (p.byGlint && !p.explorationView && !p.framingEndsOnLook) return Lead::Character;
        return !p.cameraOk || (p.needCamera && p.characterOk) ? Lead::Camera : Lead::Character;
    }

    bool GlintMissing(const Parts& p, bool insideEllipse, bool billboardInView)
    {
        // Away because she stands outside the glint's own volume, it is hers to step into, not missing;
        // its billboard out of the picture, the camera's look is what is missing.
        return p.byGlint && !p.glintAway && p.explorationView && p.measured && p.atHand && !p.verdict && insideEllipse && billboardInView &&
               (!p.onScreen || p.sightBlocked);
    }

    WalkKey WalkKeyAt(bool reached, bool way, bool pressTaken, Lead missing)
    {
        if (!reached) return WalkKey::Walk;
        if (way || pressTaken) return WalkKey::Reached;
        return missing == Lead::Camera ? WalkKey::Look : WalkKey::Walk;
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

    Vec GlintStepPoint(const Vec& useLow, const Vec& useHigh, const Vec& glintLow, const Vec& glintHigh, double atZ)
    {
        const double lowX = std::max(useLow.x, glintLow.x), highX = std::min(useHigh.x, glintHigh.x);
        const double lowY = std::max(useLow.y, glintLow.y), highY = std::min(useHigh.y, glintHigh.y);
        if (lowX <= highX && lowY <= highY) return Vec{(lowX + highX) / 2.0, (lowY + highY) / 2.0, atZ};
        return Vec{(glintLow.x + glintHigh.x) / 2.0, (glintLow.y + glintHigh.y) / 2.0, atZ};
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

    std::optional<Push> Flicker::Update(double now, double bearing, double facing, double speed, const Vec& here, bool step)
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
        // What the last push did: nothing, or a step or a turn; toward a step, a turn on the spot is
        // nothing.
        if (m_count > 0)
        {
            const bool moved = std::hypot(here.x - m_flickedFrom.x, here.y - m_flickedFrom.y) >= kDeadMove;
            const bool turned = std::fabs(Normalize(facing - m_flickedFacing)) >= kDeadTurn;
            m_dead = moved || (turned && !step) ? 0 : m_dead + 1;
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

    bool StretchKeeps(bool offered, bool begun, double fromSpot, double wayLeft, double letGoAt, bool steppingBack)
    {
        return offered || (!steppingBack && (begun || wayLeft < letGoAt) && fromSpot <= kStretchReach);
    }

    std::vector<Vec> BackOffLine(const Vec& from, const Vec& back)
    {
        return {from, back};
    }

    double FramingLeft(const Framing& f, const Vec& here)
    {
        if (!f.waitsForDistance || f.distance <= 0.0) return 0.0;
        const double gone = std::sqrt((here.x - f.start.x) * (here.x - f.start.x) + (here.y - f.start.y) * (here.y - f.start.y) +
                                      (here.z - f.start.z) * (here.z - f.start.z));
        return std::max(0.0, f.distance - gone);
    }

    Vec AwayFrom(const Vec& center, const Vec& here, double reach, double facingYaw, double turn)
    {
        double dx = here.x - center.x;
        double dy = here.y - center.y;
        double length = std::hypot(dx, dy);
        if (length < 1.0)
        {
            const double radians = facingYaw * std::numbers::pi / 180.0;
            dx = std::cos(radians);
            dy = std::sin(radians);
            length = 1.0;
        }
        const double radians = turn * std::numbers::pi / 180.0;
        const double ux = dx / length, uy = dy / length;
        const double rx = ux * std::cos(radians) - uy * std::sin(radians);
        const double ry = ux * std::sin(radians) + uy * std::cos(radians);
        return Vec{center.x + rx * reach, center.y + ry * reach, here.z};
    }

    Vec FramingExit(const Framing& f, const Vec& here, double facingYaw, double turn)
    {
        return AwayFrom(f.start, here, f.distance + kFramingMargin, facingYaw, turn);
    }

    Vec BackOffFrom(const Vec& spot, const Vec& here, double facingYaw, double turn)
    {
        return AwayFrom(spot, here, std::hypot(here.x - spot.x, here.y - spot.y) + kBackOff, facingYaw, turn);
    }

    std::optional<Vec> BackOffPoint(const std::vector<Vec>& route, double back)
    {
        if (route.empty()) return std::nullopt;
        double left = back;
        for (size_t i = route.size() - 1; i > 0; --i)
        {
            const Vec& a = route[i];
            const Vec& b = route[i - 1];
            const double leg = std::hypot(b.x - a.x, b.y - a.y);
            if (leg >= left && leg > 0.0)
            {
                const double share = left / leg;
                return Vec{a.x + (b.x - a.x) * share, a.y + (b.y - a.y) * share, a.z + (b.z - a.z) * share};
            }
            left -= leg;
        }
        return route.front();
    }

    bool GivesUp(double sinceBegun, double sinceLastPush, bool still)
    {
        if (sinceBegun <= kArriveMost) return false;
        if (sinceBegun > kArriveMost + kGiveUpWaitMost) return true;
        return still && sinceLastPush >= kSettleAfterPush;
    }

    bool PathLevelGivesUp(double sinceLevel, bool still)
    {
        if (sinceLevel >= kPathLevelWaitMost) return true;
        return still && sinceLevel >= kPathLevelSettle;
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

    StairWay StairPush(const Vec& here, const Vec& bottom, const Vec& top, const std::vector<Vec>& ahead, const Vec& aim, int last)
    {
        const double rx = top.x - bottom.x;
        const double ry = top.y - bottom.y;
        const double run = std::hypot(rx, ry);
        if (run < 1.0)
        {
            const double dx = aim.x - here.x, dy = aim.y - here.y;
            const double d = std::hypot(dx, dy);
            return StairWay{d < 1.0 ? Vec{0.0, 0.0, 0.0} : Vec{dx / d, dy / d, 0.0}, 0};
        }
        const Vec rise{rx / run, ry / run, 0.0};
        const auto along = [&](const Vec& p) { return (p.x - bottom.x) * rise.x + (p.y - bottom.y) * rise.y; };
        const double from = along(here);
        const auto sign = [](double v) { return v > 0.0 ? 1 : -1; };
        int way = 0;
        for (const Vec& c : ahead)
        {
            const double moved = along(c) - from;
            if (std::fabs(moved) > kStairDecides)
            {
                way = sign(moved);
                break;
            }
        }
        if (way == 0 && std::fabs(aim.z - here.z) > kStairDecidesRise) way = sign(aim.z - here.z);
        if (way == 0 && std::fabs(along(aim) - from) > kStairDecides) way = sign(along(aim) - from);
        if (way == 0) way = last != 0 ? last : 1;
        return StairWay{Vec{rise.x * way, rise.y * way, 0.0}, way};
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
