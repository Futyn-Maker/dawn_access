#include "features/PromptRules.hpp"

#include <algorithm>
#include <cmath>

namespace da::prompt
{
    void SetUp(Question& q, double now)
    {
        if (q.hidden || q.text.empty())
        {
            q.announced = false;
            q.since = now;
        }
        q.hidden = false;
    }

    void Hide(Question& q)
    {
        q.hidden = true;
    }

    bool Read(Question& q, double now, const std::wstring& text, const std::wstring& actions, bool glint)
    {
        if (text != q.text || actions != q.actions)
        {
            q.text = text;
            q.actions = actions;
            q.since = now;
            q.announced = false;
            return false;
        }
        if (text.empty() || q.announced || now - q.since < (glint ? kGlintStable : kPromptStable)) return false;
        q.announced = true;
        return true;
    }

    void Withheld(Question& q, bool stillUp)
    {
        if (stillUp) return;
        q.text.clear();
        q.actions.clear();
        q.announced = false;
    }

    bool CapShowsKey(bool hasAction, bool setUpFromKey)
    {
        return hasAction || setUpFromKey;
    }

    void HintFirst::Interaction(bool in)
    {
        if (in && !m_in)
        {
            m_pending = true;
            m_openedAt = -1.0;
        }
        if (!in)
        {
            m_pending = false;
            m_openedAt = -1.0;
        }
        m_in = in;
    }

    void HintFirst::Asked(double now)
    {
        if (!m_pending) return;
        m_pending = false;
        m_openedAt = now;
    }

    bool HintFirst::Waits(double now, double hintSaidAt) const
    {
        if (m_openedAt < 0.0 || hintSaidAt >= m_openedAt - kHintWait) return false;
        return now - m_openedAt < kHintWait;
    }

    std::vector<std::wstring> Bar::Update(double now, bool pageUp, const std::vector<std::wstring>& lines)
    {
        if (pageUp || lines == m_lines)
        {
            m_changedAt = -1.0;
            return {};
        }
        // The bar is read once its prompts have stopped changing for a moment.
        if (m_changedAt < 0.0) m_changedAt = now;
        if (now - m_changedAt < kBarStable) return {};
        m_changedAt = -1.0;
        m_lines = lines;
        std::set<std::wstring> current;
        std::vector<std::wstring> fresh;
        for (const auto& line : lines)
        {
            if (line.empty()) continue;
            current.insert(line);
            if (!m_said.count(line)) fresh.push_back(line);
        }
        m_said = current;
        return fresh;
    }

    Swipe::Step Swipe::Update(double now, int state, double progress, double target)
    {
        Step step;
        if (state != m_state)
        {
            const int was = m_state;
            m_state = state;
            step.stateChanged = true;
            if (state == 2)
            {
                m_level = 0.0;
                step.began = true;
            }
            if (state == 3 && was == 2) step.unlocked = true;
        }
        if (state != 2 || target <= 0.0) return step;
        const double level = std::clamp(progress / target, 0.0, 1.0);
        step.level = level;
        if (std::fabs(level - m_level) >= kBlipStep && now - m_blipAt >= kBlipApart)
        {
            m_level = level;
            m_blipAt = now;
            step.blip = true;
        }
        return step;
    }
}
