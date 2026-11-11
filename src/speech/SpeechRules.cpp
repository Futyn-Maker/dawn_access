#include "speech/SpeechRules.hpp"

#include <algorithm>

namespace da::speech::rules
{
    bool Interrupting(const std::wstring& policy)
    {
        return policy == L"focus" || policy == L"now" || policy == L"urgent" || policy == L"replace" || policy == L"repeat" || policy == L"stop";
    }

    double Speaking(const std::wstring& text)
    {
        return static_cast<double>(text.size()) / kReaderPace;
    }

    bool StillOn(const std::vector<Said>& said, const std::wstring& page, double now)
    {
        for (size_t i = said.size(); i-- > 0;)
        {
            if (said[i].text == page) return now < said[i].at + Speaking(said[i].text);
            if (Interrupting(said[i].policy)) return false;
        }
        return false;
    }

    std::vector<TurnLine> TurnOrder(const TurnPlan& plan, const std::wstring& page)
    {
        std::vector<TurnLine> out;
        if (!plan.cut)
        {
            out.push_back({page, false, L"replace-after"});
            return out;
        }
        out.push_back({page, true, L"replace"});
        for (const auto& line : plan.kept)
            out.push_back({line, false, L"replace-kept"});
        return out;
    }

    TurnPlan Turn(const std::vector<Said>& said, const std::wstring& page, double now, const std::map<std::wstring, double>* withdrawn)
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
        // Still on it: every line behind it is yet to be begun; each once, at its last place, and
        // none taken down since it was said.
        for (size_t i = from + 1; i < said.size(); ++i)
        {
            const auto& line = said[i];
            if (withdrawn)
            {
                if (const auto it = withdrawn->find(line.text); it != withdrawn->end() && it->second >= line.at)
                {
                    plan.dropped.push_back(line.text);
                    continue;
                }
            }
            bool later = false;
            for (size_t j = i + 1; j < said.size() && !later; ++j)
                later = said[j].text == line.text;
            if (!later) plan.kept.push_back(line.text);
        }
        return plan;
    }

    bool Holders::Hold(const std::wstring& owner)
    {
        const bool began = m_owners.empty();
        return m_owners.insert(owner).second && began;
    }

    bool Holders::Release(const std::wstring& owner)
    {
        return m_owners.erase(owner) > 0 && m_owners.empty();
    }
}
