#include "features/WayList.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <numbers>
#include <set>

namespace da::ways
{
    namespace
    {
        // The reached level at a column nearest `level` and within one of it; false when the map
        // reached none there.
        bool LevelNear(const grid::Flood& flood, int x, int y, int level, int& out)
        {
            const auto* levels = flood.LevelsAt(x, y);
            if (!levels) return false;
            bool found = false;
            for (const int l : *levels)
            {
                if (std::abs(l - level) > 1) continue;
                if (!found || std::abs(l - level) < std::abs(out - level))
                {
                    out = l;
                    found = true;
                }
            }
            return found;
        }

        // The straight line between two reached cells runs over reached ground all along: a column
        // for each cell of the longer side, the one nearest the line, each reached on a level
        // within one of the last, ending on the far cell's own level.
        bool ClearOverMap(const grid::Flood& flood, grid::Cell a, grid::Cell b)
        {
            const int steps = std::max(std::abs(a.x - b.x), std::abs(a.y - b.y));
            if (steps == 0) return std::abs(a.level - b.level) <= 1;
            int level = a.level;
            for (int i = 1; i <= steps; ++i)
            {
                const int x = a.x + static_cast<int>(std::lround(static_cast<double>(b.x - a.x) * i / steps));
                const int y = a.y + static_cast<int>(std::lround(static_cast<double>(b.y - a.y) * i / steps));
                if (i == steps) return x == b.x && y == b.y && std::abs(b.level - level) <= 1;
                if (!LevelNear(flood, x, y, level, level)) return false;
            }
            return true;
        }
    }

    std::vector<grid::Cell> StraightOverMap(const grid::Flood& flood, const std::vector<grid::Cell>& path, size_t maxRun)
    {
        return grid::Corners(path, [&](grid::Cell a, grid::Cell b) { return ClearOverMap(flood, a, b); }, maxRun);
    }

    std::vector<int> RowRule(const std::vector<Entry>& entries, const Enters& enters)
    {
        std::vector<size_t> order(entries.size());
        for (size_t i = 0; i < order.size(); ++i)
            order[i] = i;
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return entries[a].distance > entries[b].distance; });
        std::vector<int> onRoadOf(entries.size(), -1);
        std::set<size_t> listedWays;
        for (const size_t i : order)
        {
            const Entry& way = entries[i];
            // A verdict kept on the road of a trigger given way to the story's is no verdict.
            const bool roadGone =
                way.onRoadOf >= 0 && static_cast<size_t>(way.onRoadOf) < entries.size() && entries[static_cast<size_t>(way.onRoadOf)].superseded;
            // The scene's goal is never left out, whatever it was found on before.
            if (way.kept && !roadGone && !way.goal)
            {
                onRoadOf[i] = way.onRoadOf;
                if (way.way && way.onRoadOf < 0 && !way.behind && !way.unreachable && !way.unjudged && !way.superseded) listedWays.insert(i);
                continue;
            }
            const bool judged = way.way && way.hasBox && !way.behind && !way.unreachable && !way.unjudged && !way.superseded && !way.reached &&
                                !way.chosen && !way.goal;
            if (judged)
            {
                for (const size_t j : order)
                {
                    if (j == i) continue;
                    const Entry& other = entries[j];
                    const bool counts = !other.closedOff && !other.superseded &&
                                        (other.chosen || (!other.behind && !other.unreachable && !other.unjudged && (!other.way || listedWays.count(j) > 0)));
                    if (!counts) continue;
                    // A road not known proves nothing: the verdict on it stands as it was.
                    const auto through = enters(i, j);
                    if (!(through ? *through : way.onRoadOf == static_cast<int>(j))) continue;
                    onRoadOf[i] = static_cast<int>(j);
                    break;
                }
            }
            if (way.way && onRoadOf[i] < 0 && !way.behind && !way.unreachable && !way.unjudged && !way.superseded) listedWays.insert(i);
        }
        return onRoadOf;
    }

    std::vector<Vec> PathAtFeet(std::vector<Vec> samples, double atHer, double feet)
    {
        for (auto& p : samples)
            p.z = feet + (p.z - atHer);
        return samples;
    }

    int64_t FreezeAfter(uint64_t generation, double startedAt, double changedAt)
    {
        const auto running = static_cast<int64_t>(generation);
        return startedAt >= changedAt ? running - 1 : running;
    }

    std::vector<bool> StreamsSuperseded(const std::vector<Entry>& entries)
    {
        const bool leads = std::any_of(entries.begin(), entries.end(), [](const Entry& e)
                                       { return e.way && !e.streams && !e.unreachable && !e.behind && !e.unjudged && !e.closedOff; });
        std::vector<bool> out(entries.size(), false);
        for (size_t i = 0; i < entries.size(); ++i)
            out[i] = leads && entries[i].way && entries[i].streams;
        return out;
    }

    Box TurnedBox(const Vec& middle, const Vec& size, double yaw)
    {
        Box box;
        box.middle = middle;
        box.size = size;
        box.yaw = yaw;
        const double radians = yaw * std::numbers::pi / 180.0;
        const double c = std::fabs(std::cos(radians));
        const double s = std::fabs(std::sin(radians));
        const double hx = c * size.x / 2.0 + s * size.y / 2.0;
        const double hy = s * size.x / 2.0 + c * size.y / 2.0;
        const double hz = size.z / 2.0;
        box.low = Vec{middle.x - hx, middle.y - hy, middle.z - hz};
        box.high = Vec{middle.x + hx, middle.y + hy, middle.z + hz};
        return box;
    }

    bool CapsuleOverlaps(const Box& box, const Vec& feet, double radius)
    {
        const double radians = box.yaw * std::numbers::pi / 180.0;
        const double c = std::cos(radians);
        const double s = std::sin(radians);
        const double dx = feet.x - box.middle.x;
        const double dy = feet.y - box.middle.y;
        const double lx = dx * c + dy * s;
        const double ly = -dx * s + dy * c;
        return std::hypot(std::max(std::fabs(lx) - box.size.x / 2.0, 0.0), std::max(std::fabs(ly) - box.size.y / 2.0, 0.0)) <= radius;
    }

    bool RoadEntersBox(const Box& box, const std::vector<Vec>& road, double radius, double halfHeight, double step)
    {
        for (size_t i = 1; i < road.size(); ++i)
        {
            const Vec& a = road[i - 1];
            const Vec& b = road[i];
            const int steps = std::max(1, static_cast<int>(std::hypot(b.x - a.x, b.y - a.y) / step));
            for (int k = 0; k <= steps; ++k)
            {
                const double f = static_cast<double>(k) / steps;
                Vec p{a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f, a.z + (b.z - a.z) * f};
                if (p.x < box.low.x - radius || p.x > box.high.x + radius || p.y < box.low.y - radius || p.y > box.high.y + radius) continue;
                p.z = std::clamp((box.low.z + box.high.z) / 2.0, p.z, p.z + 2.0 * halfHeight);
                if (p.z < box.low.z || p.z > box.high.z) continue;
                if (CapsuleOverlaps(box, p, radius)) return true;
            }
        }
        return false;
    }

    void Settling::Reset()
    {
        m_settled = false;
        m_keep = false;
        m_once = false;
        m_begun = false;
        m_kept = false;
        m_startedAt = 0.0;
    }

    void Settling::Begin(double now)
    {
        m_keep = m_once;
        m_kept = m_once;
        m_settled = false;
        m_begun = true;
        m_startedAt = now;
    }

    void Settling::Clear()
    {
        m_settled = true;
        m_keep = false;
        m_kept = false;
        m_once = true;
    }

    bool Settling::Update(double now, bool done, size_t judged, size_t targets)
    {
        if (m_settled || !m_begun) return false;
        if (!done && judged < targets && now - m_startedAt < kSettleCap) return false;
        m_settled = true;
        m_keep = false;
        m_once = true;
        return true;
    }

    bool Lister::Due(const Settling& settle, bool settlesNow, double now) const
    {
        return settle.Settled() && (settlesNow || now - m_judgedAt >= 1.0);
    }

    void Lister::Judge(std::vector<Entry>& entries, const Settling& settle, bool due, bool roads, bool mapDone, double now, const Enters& enters)
    {
        // Between a hand-over and its first map nothing judges the roads: the list stands as it was,
        // and a way it has never weighed waits for a judgement that weighs it. In Josh's cave on
        // 2026-11-09 two ways the game turned on during a vision were listed unweighed at the
        // hand-over, the settling kept from the hold before, and left out a quarter of a second later
        // (research/sessions/s63-play.log, 01:08:15.647 to .904).
        if (!roads && (settle.Settled() || settle.Keeping()))
        {
            for (auto& e : entries)
            {
                if (e.way && !e.passed && !e.shown) e.unjudged = true;
            }
            return;
        }
        const bool anyShown = std::any_of(entries.begin(), entries.end(), [](const Entry& e) { return e.shown; });
        for (auto& e : entries)
        {
            if (settle.Keeping() && e.passed) continue;
            const bool waitsForMap = !mapDone && anyShown && !e.shown;
            // Between two judgements a way the rule has not weighed waits for the next.
            const bool unweighed = !due && !Weighed(e);
            e.unjudged = e.way && (!settle.Settled() || waitsForMap || !(e.known || (settle.Kept() && e.earlier)) || unweighed);
        }
        // Which triggers give way to the story's, by the ways as this look judges them.
        const auto superseded = StreamsSuperseded(entries);
        for (size_t i = 0; i < entries.size(); ++i)
            entries[i].superseded = superseded[i];
        if (!due) return;
        m_judgedAt = now;
        // A verdict on the road of a trigger given way to the story's is not kept (RowRule).
        for (auto& e : entries)
        {
            const bool roadGone =
                e.onRoadOf >= 0 && static_cast<size_t>(e.onRoadOf) < entries.size() && entries[static_cast<size_t>(e.onRoadOf)].superseded;
            e.kept = Weighed(e) && (m_frozen || (e.way && e.shown)) && !roadGone;
        }
        const auto onRoadOf = RowRule(entries, enters);
        for (size_t i = 0; i < entries.size(); ++i)
        {
            // A way the rule could not weigh keeps no verdict of this judgement's, nor an older one
            // it did not keep: it waits for a judgement that weighs it, the road it was last found on
            // kept meanwhile (the rule gave it none, and the ways on the bolt door's road in act7_2
            // were logged offered as they waited, research/sessions/s63-play.log 00:22:51.623).
            if (entries[i].way && entries[i].unjudged)
            {
                if (!entries[i].kept) entries[i].passed = false;
                continue;
            }
            entries[i].onRoadOf = onRoadOf[i];
            entries[i].passed = true;
            entries[i].epoch = m_epoch;
            entries[i].hold = m_hold;
        }
    }

    std::optional<double> LeftAlong(const std::vector<Vec>& corners, const Vec& here, bool& lastLeg)
    {
        lastLeg = false;
        const int on = StretchOn(corners, corners.size(), here, 150.0, kStretchRise);
        if (on < 0) return std::nullopt;
        const auto flat = [](const Vec& a, const Vec& b) { return std::hypot(a.x - b.x, a.y - b.y); };
        size_t next = static_cast<size_t>(on);
        while (next + 1 < corners.size() && flat(here, corners[next]) < 60.0)
            ++next;
        double length = flat(here, corners[next]);
        for (size_t i = next + 1; i < corners.size(); ++i)
            length += flat(corners[i - 1], corners[i]);
        lastLeg = next + 1 == corners.size();
        return length;
    }

    double SpokenDistance(double straight, std::optional<double> road, std::optional<double> map)
    {
        if (road) return std::max(straight, *road);
        if (map) return std::max(straight, *map);
        return straight;
    }

    std::optional<double> MapMeasure(bool judged, bool reached, double targetCost, bool running, bool cutShort, double frontier,
                                     std::optional<double> herCost)
    {
        if (!herCost) return std::nullopt;
        if (judged)
        {
            if (!reached) return std::nullopt;
            return std::max(0.0, targetCost - *herCost);
        }
        if (!running && !cutShort) return std::nullopt;
        return std::max(0.0, frontier - *herCost);
    }

    bool IsNews(std::optional<double> turnedOnAt, double now, bool listedBefore)
    {
        return !listedBefore && turnedOnAt && now - *turnedOnAt <= kNewsWithin;
    }

    bool ResumesList(bool paused, bool onMappedGround)
    {
        return paused || onMappedGround;
    }

    bool MapProves(bool complete, bool radiusReached, int expanded, int minCells)
    {
        return complete && !radiusReached && expanded >= minCells;
    }

    bool StreamsMore(std::optional<bool> sectionLoaded, bool inside, bool seenBefore)
    {
        return sectionLoaded.has_value() && !*sectionLoaded && !inside && !seenBefore;
    }

    void LevelsSeen::Count(const std::vector<std::wstring>& levels, bool newWorld)
    {
        if (newWorld) m_seen.clear();
        for (const auto& level : levels)
            Loaded(level);
    }

    void LevelsSeen::Loaded(const std::wstring& section)
    {
        if (!Seen(section)) m_seen.push_back(section);
    }

    bool LevelsSeen::Seen(const std::wstring& section) const
    {
        return std::find(m_seen.begin(), m_seen.end(), section) != m_seen.end();
    }

    bool BeamWay(bool volumeOn, bool onBeam, bool crossed)
    {
        return volumeOn && !onBeam && !crossed;
    }

    bool BeamCrossed(const Vec& tookAt, const Vec& letGo)
    {
        return std::hypot(letGo.x - tookAt.x, letGo.y - tookAt.y) >= kBeamCrossed;
    }

    int KeptReach(int reach, bool sameGround)
    {
        return sameGround && (reach == 1 || reach == 3) ? reach : 0;
    }

    bool StillComing(bool mapRunning, size_t waiting)
    {
        return mapRunning && waiting > 0;
    }

    bool MapHurries(bool settled, bool keeping, bool anyListed, size_t waiting)
    {
        return !anyListed || (!settled && !keeping) || waiting > 0;
    }

    bool OfferHold::Update(bool otherOffered, double now)
    {
        if (!otherOffered)
        {
            m_since = -1.0;
            return false;
        }
        if (m_since < 0.0) m_since = now;
        return now - m_since >= kOtherOfferHold;
    }

    bool HintWaitsForTurn(bool newTurn, bool cardUp, bool cardSeen, bool objectiveDue, double sinceHandOver)
    {
        if (!newTurn || sinceHandOver >= kTurnWait) return false;
        if (cardUp) return true;
        if (!cardSeen) return sinceHandOver < kCardWait;
        return objectiveDue;
    }

    int HandOverChoice(int chosen, bool autoChosen, const std::vector<bool>& shown)
    {
        const bool known = chosen >= 0 && static_cast<size_t>(chosen) < shown.size();
        if (known && (shown[static_cast<size_t>(chosen)] || !autoChosen)) return chosen;
        for (size_t i = 0; i < shown.size(); ++i)
        {
            if (shown[i]) return static_cast<int>(i);
        }
        return -1;
    }

    bool Unplaced(const Vec& root)
    {
        return std::fabs(root.x) < 1.0 && std::fabs(root.y) < 1.0 && std::fabs(root.z) < 1.0;
    }

    bool WalkGoesOn(const void* walkFor, const void* chosen)
    {
        return walkFor != nullptr && chosen == walkFor;
    }

    bool WalkLosesTarget(bool closedOffNow, bool closedOffAtStart, bool worldChanged)
    {
        return closedOffNow && !closedOffAtStart && worldChanged;
    }

    bool WorldChangedSince(double streamedAt, double groundChangedAt, double since)
    {
        return streamedAt > since || groundChangedAt > since;
    }

    bool GroundChangeTells(size_t changed)
    {
        return changed >= kWorldChangeSteps;
    }

    bool WaitsForEntry(bool firesOnEntry, bool polled, bool standingIn)
    {
        return firesOnEntry && !polled && standingIn;
    }

    bool MovedFromMap(double fromOrigin)
    {
        return fromOrigin > kMovedFromMap;
    }

    const void* WalkGoesTo(const void* walkFor, const void* chosen, const void* twin)
    {
        if (WalkGoesOn(walkFor, chosen)) return walkFor;
        return walkFor != nullptr ? twin : nullptr;
    }

    bool TakesWayPlace(const LeftOut& thing)
    {
        if (!thing.hidden) return true;
        return !thing.behind && !thing.offPath && !thing.unreachable && !thing.superseded && !thing.onRoad && !thing.apart;
    }

    StopBy WhoStops(bool groundWay)
    {
        return groundWay ? StopBy::Him : StopBy::Ground;
    }

    bool BacksOffFrom(bool alreadyBackedOff, bool groundAsked)
    {
        return groundAsked && !alreadyBackedOff;
    }

    std::vector<double> KeepClearances(double hers, double his)
    {
        return {hers + his + kKeepClearMargin, hers + his};
    }

    bool KeepClear::Clear(const Vec& at) const
    {
        return std::none_of(standing.begin(), standing.end(), [&](const Vec& o) { return std::hypot(at.x - o.x, at.y - o.y) < clearance; });
    }

    bool KeepClear::StepClear(const Vec& from, const Vec& to) const
    {
        if (Clear(to)) return true;
        return std::all_of(standing.begin(), standing.end(),
                           [&](const Vec& o) { return std::hypot(to.x - o.x, to.y - o.y) >= std::hypot(from.x - o.x, from.y - o.y); });
    }

    bool KeepClear::RunClear(const Vec& a, const Vec& b) const
    {
        for (const Vec& o : standing)
        {
            const double dx = b.x - a.x, dy = b.y - a.y;
            const double length = dx * dx + dy * dy;
            const double f = length > 0.0 ? std::clamp(((o.x - a.x) * dx + (o.y - a.y) * dy) / length, 0.0, 1.0) : 0.0;
            if (std::hypot(a.x + dx * f - o.x, a.y + dy * f - o.y) < clearance) return false;
        }
        return true;
    }

    int InTheWay(const Vec& here, const Vec& toward, const std::vector<Vec>& others)
    {
        const double wx = toward.x - here.x, wy = toward.y - here.y;
        const double way = std::hypot(wx, wy);
        if (way < 1.0) return -1;
        int best = -1;
        double nearest = kInTheWayReach;
        for (size_t i = 0; i < others.size(); ++i)
        {
            const double ox = others[i].x - here.x, oy = others[i].y - here.y;
            const double apart = std::hypot(ox, oy);
            if (apart > nearest || apart < 1.0) continue;
            const double cosine = (ox * wx + oy * wy) / (apart * way);
            if (cosine < std::cos(kInTheWayAngle * std::numbers::pi / 180.0)) continue;
            nearest = apart;
            best = static_cast<int>(i);
        }
        return best;
    }

    bool WalkEndsArrived(bool wayVolume, bool inside)
    {
        return wayVolume && inside;
    }

    bool Reopened(int reachWhenFrozen, int reachNow)
    {
        return (reachWhenFrozen == 2 || reachWhenFrozen == 3) && reachNow == 1;
    }

    double PathStray(double ahead, double coneDegrees)
    {
        const double cone = std::clamp(std::fabs(coneDegrees), 0.0, 60.0) * std::numbers::pi / 180.0;
        return std::clamp(std::max(0.0, ahead) * std::tan(cone), 0.0, kPathStrayMax);
    }

    bool LeftForThing(const Vec& thing, const Vec& way, bool thingGoneTo)
    {
        if (!thingGoneTo) return false;
        return std::hypot(thing.x - way.x, thing.y - way.y) < kTwinAcross && std::fabs(thing.z - way.z) < kTwinUp;
    }

    double VolumeGoalRadius(const Box& box, double capsuleRadius, double cell)
    {
        const double half = std::min(box.high.x - box.low.x, box.high.y - box.low.y) / 2.0;
        return std::max(1.5, (half + capsuleRadius) / cell);
    }

    bool OnlyTried(const std::vector<std::wstring>& animations)
    {
        return !animations.empty() &&
               std::all_of(animations.begin(), animations.end(), [](const std::wstring& a) { return a.rfind(L"ABP_Door_Locked_", 0) == 0; });
    }

    bool UsableBy(const UseRule& rule)
    {
        if (rule.requiresAnimation && !rule.animated) return false;
        return rule.anyone || rule.hers;
    }

    bool InRange(double distance, double range, bool listedBefore)
    {
        return distance <= range + (listedBefore ? kRangeMargin : 0.0);
    }

    std::vector<size_t> StoodIn(const std::vector<bool>& inside)
    {
        std::vector<size_t> out;
        for (size_t i = 0; i < inside.size(); ++i)
        {
            if (inside[i]) out.push_back(i);
        }
        return out;
    }

    int StretchOn(const std::vector<Vec>& corners, size_t count, const Vec& here, double reach, double rise)
    {
        count = std::min(count, corners.size());
        if (count == 0) return -1;
        int stretch = -1;
        double best = reach;
        const auto consider = [&](const Vec& point, int index)
        {
            if (std::fabs(here.z - point.z) > rise) return;
            const double over = std::hypot(here.x - point.x, here.y - point.y);
            if (over < best)
            {
                best = over;
                stretch = index;
            }
        };
        consider(corners[0], 0);
        for (size_t i = 1; i < count; ++i)
        {
            const Vec& a = corners[i - 1];
            const Vec& b = corners[i];
            const double abx = b.x - a.x;
            const double aby = b.y - a.y;
            const double length = abx * abx + aby * aby;
            double along = 0.0;
            if (length > 1.0) along = std::clamp(((here.x - a.x) * abx + (here.y - a.y) * aby) / length, 0.0, 1.0);
            consider(Vec{a.x + abx * along, a.y + aby * along, a.z + (b.z - a.z) * along}, static_cast<int>(i));
        }
        return stretch;
    }

    std::optional<Lead> LeadCorner(const std::vector<Vec>& corners, size_t count, const Vec& here, double reach, double rise)
    {
        count = std::min(count, corners.size());
        const int on = StretchOn(corners, count, here, reach, rise);
        if (on < 0) return std::nullopt;
        const auto flat = [](const Vec& a, const Vec& b) { return std::hypot(a.x - b.x, a.y - b.y); };
        size_t next = static_cast<size_t>(on);
        while (next + 1 < count)
        {
            if (flat(here, corners[next]) < kCornerPassed)
            {
                ++next;
                continue;
            }
            if (next == 0) break;
            // Beyond the corner along the stretch that ends there.
            const Vec& a = corners[next - 1];
            const Vec& b = corners[next];
            const double abx = b.x - a.x;
            const double aby = b.y - a.y;
            const double length = abx * abx + aby * aby;
            if (length <= 1.0 || ((here.x - a.x) * abx + (here.y - a.y) * aby) / length < 1.0) break;
            ++next;
        }
        Lead lead;
        lead.corner = next;
        lead.length = flat(here, corners[next]);
        for (size_t i = next + 1; i < count; ++i)
            lead.length += flat(corners[i - 1], corners[i]);
        return lead;
    }

    Vec SlideWalk(const Sweep& sweep, const Vec& from, const Vec& to, int& touches)
    {
        Vec pos = from;
        touches = 0;
        for (int i = 0; i <= kPassageTouches; ++i)
        {
            const double length = std::sqrt((to.x - pos.x) * (to.x - pos.x) + (to.y - pos.y) * (to.y - pos.y) + (to.z - pos.z) * (to.z - pos.z));
            if (length <= 1.0) return to;
            const SweepHit hit = sweep(pos, to);
            if (!hit.hit) return to;
            if (i == kPassageTouches) break;
            ++touches;
            double nx = hit.normal.x;
            double ny = hit.normal.y;
            const double n = std::hypot(nx, ny);
            if (n < 1e-3) break; // a floor or a ceiling across the way
            nx /= n;
            ny /= n;
            if (hit.penetrating)
            {
                // Begun pressed in: out along the contact, as the movement pulls her out.
                pos = Vec{pos.x + nx * (hit.depth + 1.0), pos.y + ny * (hit.depth + 1.0), pos.z};
                continue;
            }
            // Up to the contact, a hair short of it and a hair off the surface.
            const double go = std::clamp(hit.distance - 0.5, 0.0, length);
            const double f = go / length;
            pos = Vec{pos.x + (to.x - pos.x) * f + nx * 0.2, pos.y + (to.y - pos.y) * f + ny * 0.2, pos.z + (to.z - pos.z) * f};
            // The rest of the move along the surface: its part into the surface taken away.
            double rx = to.x - pos.x;
            double ry = to.y - pos.y;
            const double into = rx * nx + ry * ny;
            if (into < 0.0)
            {
                rx -= nx * into;
                ry -= ny * into;
            }
            const double rest = std::hypot(rx, ry);
            if (rest < 1.0) break; // head on
            const Vec along{pos.x + rx, pos.y + ry, pos.z};
            const SweepHit slid = sweep(pos, along);
            if (!slid.hit)
            {
                pos = along;
                continue;
            }
            if (slid.penetrating) break;
            const double g = std::clamp(slid.distance - 0.5, 0.0, rest) / rest;
            pos = Vec{pos.x + rx * g, pos.y + ry * g, pos.z};
        }
        return pos;
    }

    StepSweep SweepOut(const Sweep& sweep, const Vec& from, const Vec& to)
    {
        StepSweep out;
        Vec f = from;
        Vec t = to;
        for (int attempt = 0; attempt < 2; ++attempt)
        {
            const double distance = std::sqrt((t.x - f.x) * (t.x - f.x) + (t.y - f.y) * (t.y - f.y) + (t.z - f.z) * (t.z - f.z));
            const SweepHit hit = sweep(f, t);
            if (!hit.hit)
            {
                out.clear = distance;
                out.reached = t;
                return out;
            }
            if (!hit.penetrating || attempt == 1)
            {
                out.hit = true;
                out.clear = std::clamp(hit.distance, 0.0, distance);
                const double share = distance > 0.0 ? out.clear / distance : 0.0;
                out.reached = Vec{f.x + (t.x - f.x) * share, f.y + (t.y - f.y) * share, f.z + (t.z - f.z) * share};
                out.stuck = attempt == 1 && (hit.penetrating || out.clear < kStuckClear);
                return out;
            }
            const double push = hit.depth + 1.0;
            f = Vec{f.x + hit.normal.x * push, f.y + hit.normal.y * push, f.z + hit.normal.z * push};
            t = Vec{t.x + hit.normal.x * push, t.y + hit.normal.y * push, t.z + hit.normal.z * push};
        }
        return out;
    }

    std::optional<Vec> SweepFromFreeSpot(const Sweep& sweep, const Vec& from, const Vec& to, double reach)
    {
        const double dx = to.x - from.x;
        const double dy = to.y - from.y;
        const double length = std::hypot(dx, dy);
        if (length < 1e-6 || reach <= 0.0) return std::nullopt;
        const double ux = dx / length;
        const double uy = dy / length;
        // Across the way first, then back from it, then ahead, as degrees off the step's way.
        static constexpr double kTurns[] = {90.0, -90.0, 135.0, -135.0, 180.0, 45.0, -45.0, 0.0};
        for (const double ring : kFreeSpotRings)
        {
            for (const double turn : kTurns)
            {
                const double a = turn * std::numbers::pi / 180.0;
                const double ox = (ux * std::cos(a) - uy * std::sin(a)) * ring * reach;
                const double oy = (ux * std::sin(a) + uy * std::cos(a)) * ring * reach;
                const Vec spot{from.x + ox, from.y + oy, from.z};
                if (sweep(spot, Vec{spot.x + ux, spot.y + uy, spot.z}).hit) continue;
                // She stands here: the step is walked from here, and from no other spot, as her
                // movement walks her, along what she touches (SlideWalk).
                int touches = 0;
                const Vec reached = SlideWalk(sweep, spot, to, touches);
                if (std::hypot(reached.x - to.x, reached.y - to.y) > reach) return std::nullopt;
                return reached;
            }
        }
        return std::nullopt;
    }

    PassageWay PassageWalk(const Sweep& sweep, const Vec& a, const Vec& b)
    {
        PassageWay way;
        const double dx = b.x - a.x;
        const double dy = b.y - a.y;
        const double length = std::hypot(dx, dy);
        if (length < 1.0) return way;
        // The engine's frame: x ahead, y to the right.
        const double rightX = -dy / length;
        const double rightY = dx / length;
        const int steps = static_cast<int>(std::floor(kPassageShiftMax / kPassageShift + 1e-9));
        for (int k = 0; k <= 2 * steps; ++k)
        {
            const double shift = (k == 0 ? 0.0 : ((k + 1) / 2) * kPassageShift * (k % 2 == 1 ? 1.0 : -1.0));
            const Vec from{a.x + rightX * shift, a.y + rightY * shift, a.z};
            const Vec to{b.x + rightX * shift, b.y + rightY * shift, b.z};
            int touches = 0;
            const Vec reached = SlideWalk(sweep, from, to, touches);
            if (k == 0) way.reached = reached;
            if (std::hypot(reached.x - to.x, reached.y - to.y) <= kPassageArrive && std::fabs(reached.z - to.z) <= kPassageArrive)
            {
                way.passed = true;
                way.shift = shift;
                way.touches = touches;
                return way;
            }
        }
        return way;
    }

    void PassageLinks(const std::vector<Passage>& passages, const Vec& middle, double cellSize,
                      const std::function<bool(const Vec& end, grid::Cell& cell, Vec& cellMiddle)>& cellAt, std::vector<grid::Link>& out)
    {
        for (const Passage& passage : passages)
        {
            if (!passage.free) continue;
            for (int side = 0; side < 2; ++side)
            {
                const Vec& here = side == 0 ? passage.a : passage.b;
                const Vec& there = side == 0 ? passage.b : passage.a;
                if (std::hypot(middle.x - here.x, middle.y - here.y) > kPassageReach || std::fabs(middle.z - here.z) > kPassageRise) continue;
                grid::Cell cell;
                Vec cellMiddle;
                if (!cellAt(there, cell, cellMiddle)) continue;
                out.push_back(grid::Link{cell, std::hypot(cellMiddle.x - middle.x, cellMiddle.y - middle.y) / cellSize});
            }
        }
    }
}
