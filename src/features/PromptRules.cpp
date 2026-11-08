#include "features/PromptRules.hpp"

#include <algorithm>
#include <cmath>
#include <cwctype>

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

    bool IdleAfterStep(std::wstring_view state)
    {
        const size_t at = state.rfind(L"_idle_");
        if (at == std::wstring_view::npos || at == 0) return false;
        const std::wstring_view number = state.substr(at + 6);
        if (number.empty() || number.size() > 2) return false;
        for (const wchar_t c : number)
        {
            if (c < L'0' || c > L'9') return false;
        }
        return number != L"0";
    }

    std::vector<int> MachineIndices(const std::vector<bool>& nodeIsMachine)
    {
        std::vector<int> out;
        const int count = static_cast<int>(nodeIsMachine.size());
        for (int j = 0; j < count; ++j)
        {
            if (nodeIsMachine[j]) out.push_back(count - 1 - j);
        }
        return out;
    }

    bool WaitingState(std::wstring_view state)
    {
        std::wstring lower(state);
        for (auto& c : lower)
            c = static_cast<wchar_t>(std::towlower(c));
        return lower.find(L"idle") != std::wstring::npos || lower.find(L"loop") != std::wstring::npos;
    }

    bool TimeoutTaken(const std::vector<std::wstring>& atLimit, const std::vector<std::wstring>& now)
    {
        const bool waited = std::any_of(atLimit.begin(), atLimit.end(), [](const std::wstring& st) { return WaitingState(st); });
        return waited && now != atLimit;
    }

    std::optional<double> LinearProgress::Update(int64_t step, int64_t last)
    {
        if (step < 0 || last <= 0) return std::nullopt;
        const bool first = m_step < 0;
        const bool changed = step != m_step;
        m_step = step;
        if (first || !changed) return std::nullopt;
        return std::clamp(static_cast<double>(step) / static_cast<double>(last), 0.0, 1.0);
    }

    std::wstring LinearForward(std::wstring_view action, bool invert)
    {
        const auto ends = [&](std::wstring_view tail) { return action.size() > tail.size() && action.substr(action.size() - tail.size()) == tail; };
        // The one-way actions keep an underscore before their way: IA_InteractionMovement_LeftRight
        // has IA_InteractionMovement_Left, IA_SwipeLeftRight has IA_Swipe_Left.
        const auto oneWay = [&](size_t tail, const wchar_t* way)
        {
            std::wstring base(action.substr(0, action.size() - tail));
            if (base.back() != L'_') base += L'_';
            return base + way;
        };
        if (ends(L"LeftRight")) return oneWay(9, invert ? L"Left" : L"Right");
        if (ends(L"UpDown")) return oneWay(6, invert ? L"Down" : L"Up");
        return {};
    }

    std::optional<Dial> DialOf(std::wstring_view interactionClass)
    {
        if (interactionClass == L"BP_Radio_C") return Dial{L"RadioDialPosition", 3, L"IA_RotateAxisGesture", L"IA_RotateRightGesture"};
        return std::nullopt;
    }

    std::optional<Sides> SidesOf(std::wstring_view interactionClass)
    {
        // DA_CharacterMeta_Ashley and _Josh: LocalisedName SMG013_66601 and SMG013_66604.
        if (interactionClass == L"BP_Deathmachinelever_C") return Sides{L"SMG013_66601", L"SMG013_66604", L"grab_idle"};
        return std::nullopt;
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
    const wchar_t* ShakeHelp(bool pad, bool playStation, bool motionSensor)
    {
        if (!pad) return L"help.shake";
        if (!playStation) return L"help.shake.pad";
        return motionSensor ? L"help.shake.ps" : L"help.shake.ps.off";
    }
}
