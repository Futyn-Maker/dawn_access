#include "features/QteRules.hpp"

#include "core/Strings.hpp"

namespace da::qte
{
    Outcome OutcomeOf(long long result)
    {
        switch (result)
        {
        case 1: return Outcome::Success;
        case 2: return Outcome::Failure;
        case 3: return Outcome::Timeout;
        default: return Outcome::None;
        }
    }

    Direction DirectionOf(std::wstring_view action)
    {
        const auto lower = str::ToLower(std::wstring(action));
        struct Word
        {
            const wchar_t* text;
            Direction direction;
        };
        static const Word kWords[] = {{L"up", Direction::Up},       {L"top", Direction::Up},    {L"down", Direction::Down},
                                      {L"bottom", Direction::Down}, {L"left", Direction::Left}, {L"right", Direction::Right}};
        Direction direction = Direction::Up;
        size_t best = std::wstring::npos;
        for (const auto& word : kWords)
        {
            const size_t at = lower.rfind(word.text);
            if (at != std::wstring::npos && (best == std::wstring::npos || at > best))
            {
                best = at;
                direction = word.direction;
            }
        }
        return direction;
    }

    bool SameSetUp(std::wstring_view knownAction, double knownAt, std::wstring_view action, double now)
    {
        return knownAction == action && now - knownAt < kSameSetUp;
    }

    bool Running(const Prompt& p)
    {
        return !p.finished && !p.demonstration && !p.paused;
    }

    Step Poll(Prompt& p, double now, bool drawn, const Stage& stage)
    {
        Step step;
        if (drawn || now - p.setUpAt <= kFirstSecond)
            p.hiddenSince = -1.0;
        else if (p.hiddenSince < 0.0)
            p.hiddenSince = now;
        if (stage.known && stage.live)
        {
            if (stage.verdict > 0)
            {
                step.finish = true;
                step.result = stage.verdict;
                step.from = L"the stage";
                return step;
            }
            if (!stage.drivesIt || !stage.started)
            {
                step.finish = true;
                step.from = L"the stage, which moved on";
                return step;
            }
        }
        if (p.hiddenSince >= 0.0 && now - p.hiddenSince > (stage.known ? kHiddenWithStage : 0.0))
        {
            step.finish = true;
            step.from = L"the prompt, which went away";
            return step;
        }
        step.repeat = !p.paused && !p.demonstration && now - p.spokenAt >= kRepeat && (p.opened || now - p.setUpAt >= p.warmup);
        return step;
    }

    bool CuesOpening(double sinceSetUp)
    {
        return sinceSetUp >= kOpeningApart;
    }

    bool DemoRow(std::wstring_view controlName)
    {
        return controlName.starts_with(L"Setting_") && controlName.find(L"QTE") != std::wstring_view::npos;
    }

    bool Demo::Focus(std::wstring_view row, std::wstring_view value)
    {
        const bool moved = row != m_row || value != m_value;
        m_row = row;
        m_value = value;
        if (!moved || !m_playing) return false;
        m_playing = false;
        return true;
    }

    Demo::Toggle Demo::Toggled()
    {
        if (m_row.empty() || !DemoRow(m_row)) return Toggle::NotHere;
        m_playing = !m_playing;
        return m_playing ? Toggle::On : Toggle::Off;
    }

    bool Examples::Says(const std::wstring& text, double now)
    {
        if (text == m_last && now - m_at < kExamplesApart) return false;
        m_last = text;
        m_at = now;
        return true;
    }

    ExampleStep Example(double setUpAt, double now, bool shown)
    {
        if (!shown) return ExampleStep::Drop;
        return now - setUpAt < kExampleSettle ? ExampleStep::Wait : ExampleStep::Say;
    }
}
