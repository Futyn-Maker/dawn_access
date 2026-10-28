#include "features/ChoiceRules.hpp"

#include <algorithm>
#include <utility>

namespace da::choice
{
    End EndOf(long long result)
    {
        switch (result)
        {
        case 0: return End::None;
        case 1: return End::Left;
        case 2: return End::Right;
        case 3: return End::TimedOut;
        default: return End::Other;
        }
    }

    bool Hold(Option& option, double progress)
    {
        const double previous = option.selection;
        option.selection = progress;
        option.peak = std::max(option.peak, progress);
        if (progress < option.peak - 0.1) option.emptying = true;
        const bool began = progress > 0.05 && (previous <= 0.05 || (option.emptying && progress > previous + 1e-4));
        if (progress <= 0.05 || began)
        {
            option.peak = progress;
            option.emptying = false;
        }
        return began;
    }

    void Book::SetUp(const Option& option, double now)
    {
        Choice* choice = m_choices.empty() ? nullptr : &m_choices.back();
        const bool joins = choice && !choice->announced && !choice->finished && now - choice->firstSetUpAt < kJoinWithin &&
                           std::none_of(choice->options.begin(), choice->options.end(), [&](const Option& o) { return o.direction == option.direction; });
        if (!joins)
        {
            m_choices.push_back(Choice{});
            choice = &m_choices.back();
            choice->firstSetUpAt = now;
        }
        for (auto& other : m_choices)
            std::erase_if(other.options, [&](const Option& o) { return o.widget == option.widget; });
        choice->options.push_back(option);
        if (m_choices.size() > kChoicesKept) m_choices.erase(m_choices.begin());
    }

    Choice* Book::Of(Handle widget, Option** option)
    {
        for (auto choice = m_choices.rbegin(); choice != m_choices.rend(); ++choice)
        {
            for (auto& o : choice->options)
            {
                if (o.widget == widget)
                {
                    if (option) *option = &o;
                    return &*choice;
                }
            }
        }
        return nullptr;
    }

    End Book::Finish(Choice& choice, long long result, std::wstring* chosen)
    {
        const End end = EndOf(result);
        if (choice.finished || end == End::None) return End::None;
        choice.finished = true;
        if ((end == End::Left || end == End::Right) && chosen)
        {
            const long long direction = end == End::Left ? 0 : 1;
            for (const auto& o : choice.options)
            {
                if (o.direction == direction && !o.text.empty()) *chosen = o.text;
            }
        }
        return end;
    }

    bool Book::DueToRead(const Choice& c, double now)
    {
        if (c.finished || c.announced || c.options.empty()) return false;
        return c.options.size() >= 2 || now - c.firstSetUpAt >= kReadAfter;
    }

    bool Book::Gone(const Choice& c, double now)
    {
        return !c.finished && c.announced && c.options.empty() && now - c.firstSetUpAt > kReadAfter;
    }

    void Book::Forget(double now)
    {
        std::erase_if(m_choices, [&](const Choice& c) { return c.finished && now - c.firstSetUpAt > kForgetAfter; });
    }

    bool InProgress(const Choice& c)
    {
        return c.announced && !c.finished && !c.options.empty();
    }

    void Chosen::Set(std::wstring text, double now)
    {
        m_text = std::move(text);
        m_since = now;
    }

    std::wstring Chosen::Due(double now, bool held)
    {
        if (m_text.empty() || (held && now - m_since <= kChosenWait)) return {};
        return std::exchange(m_text, std::wstring());
    }
}
