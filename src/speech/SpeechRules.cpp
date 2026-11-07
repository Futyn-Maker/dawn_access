#include "speech/SpeechRules.hpp"

#include <algorithm>

namespace da::speech::rules
{
    bool Interrupting(const std::wstring& policy)
    {
        return policy == L"focus" || policy == L"now" || policy == L"urgent" || policy == L"replace" || policy == L"replace-kept" ||
               policy == L"repeat" || policy == L"stop";
    }

    double Speaking(const std::wstring& text)
    {
        return static_cast<double>(text.size()) / kReaderPace;
    }

    TurnPlan Turn(const std::vector<Said>& said, const std::wstring& page, double now)
    {
        TurnPlan plan;
        // The page as last said, with nothing cutting in after it.
        size_t from = said.size();
        for (size_t i = said.size(); i-- > 0;)
        {
            if (said[i].text == page)
            {
                from = i;
                break;
            }
            if (Interrupting(said[i].policy)) return plan;
        }
        if (from == said.size()) return plan;
        // The reader past the page: nothing of it to cut.
        if (said[from].at + Speaking(said[from].text) <= now)
        {
            plan.cut = false;
            return plan;
        }
        // Still on it: every line behind it is yet to be begun.
        for (size_t i = from + 1; i < said.size(); ++i)
            plan.kept.push_back(said[i].text);
        return plan;
    }
}
