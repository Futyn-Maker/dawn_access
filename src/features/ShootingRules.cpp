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
        if (e.still) return Verdict::None;
        // A shot counts first: in a stage that waits for its end marker after its time, a
        // highlighted shot still takes its target once the clock has run out.
        if (e.shotCircle || e.chosenChanged) return Verdict::Hit;
        if (e.aimingOnly && e.aimReached) return Verdict::AimReached;
        const bool clockOut = e.timed && e.limit > 0.0 && e.seconds >= e.limit - kClockSlack;
        if ((e.timedOutNow && !e.timedOutBefore) || clockOut) return Verdict::TimedOut;
        return Verdict::None;
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

    bool Cadence::Due(double now, bool onTarget, double interval)
    {
        const bool rose = onTarget && !m_onTarget;
        m_onTarget = onTarget;
        if (!rose && now - m_last < interval) return false;
        m_last = now;
        return true;
    }
}
