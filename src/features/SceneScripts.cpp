#include "features/SceneScripts.hpp"

#include "core/Strings.hpp"

#include <algorithm>
#include <iterator>
#include <regex>
#include <set>

namespace da::scripts
{
    namespace
    {
        // The calls that move a scene on: a film, the next level, a teleport, the player put in
        // another character, a cut to an activity, a sequence, an order to start an activity, the
        // totem's flow, the lessons' hold on walking let go, a new objective.
        const wchar_t* const kStoryCalls[] = {L"SF_PlayMovie",          L"SF_LoadNextLevel",         L"SF_Teleport",
                                              L"SF_SetPlayerCharacter", L"SF_CutToActivity",         L"SF_StartSequence",
                                              L"SF_OrderStartActivity", L"SF_StartTotemFlowCommand", L"SF_SetAllowLocomotionInTutorials",
                                              L"SF_SetCurrentObjective"};
        // A companion sent on ahead: led or moved, not escorting the player, who leads then.
        const wchar_t* const kSentOnCalls[] = {L"SF_OrderLeadTo", L"SF_OrderMoveTo"};

        std::vector<std::wstring> CallsIn(const std::wstring& text)
        {
            static const std::wregex call(LR"re((SF_[A-Za-z0-9_]+)\s*\()re");
            std::vector<std::wstring> calls;
            for (auto it = std::wsregex_iterator(text.begin(), text.end(), call); it != std::wsregex_iterator(); ++it)
                calls.push_back((*it)[1].str());
            return calls;
        }

        // The calls that start an activity or drive one on; turning one on or off (SF_SetEnableActivity)
        // only makes it available, and the thing it makes available is listed for itself.
        const wchar_t* const kStartingCalls[] = {L"SF_StartActivity", L"SF_SendMessageToActivity", L"SF_CutToActivity", L"SF_OrderStartActivity"};

        // The activities the calls in a text start or drive on.
        std::vector<std::wstring> ActivitiesIn(const std::wstring& text)
        {
            static const std::wregex call(LR"re((SF_[A-Za-z0-9_]+)\s*\(\s*sm\s*,\s*\{([^}]*)\})re");
            static const std::wregex activity(LR"re(activit(?:y|ies)\s*=\s*"([^"]*)")re");
            std::vector<std::wstring> activities;
            for (auto it = std::wsregex_iterator(text.begin(), text.end(), call); it != std::wsregex_iterator(); ++it)
            {
                const auto name = (*it)[1].str();
                if (std::find(std::begin(kStartingCalls), std::end(kStartingCalls), name) == std::end(kStartingCalls)) continue;
                const auto args = (*it)[2].str();
                std::wsmatch m;
                if (!std::regex_search(args, m, activity)) continue;
                for (const auto& one : str::Split(m[1].str(), L','))
                {
                    if (!one.empty()) activities.push_back(one);
                }
            }
            return activities;
        }

        void AddUnique(std::vector<std::wstring>& to, const std::vector<std::wstring>& from)
        {
            for (const auto& one : from)
            {
                if (one == L"SF_Start") continue;
                if (std::find(to.begin(), to.end(), one) == to.end()) to.push_back(one);
            }
        }

        bool SentOn(std::wstring_view call)
        {
            return std::find(std::begin(kSentOnCalls), std::end(kSentOnCalls), call) != std::end(kSentOnCalls);
        }

        bool MovesOn(const std::vector<std::wstring>& calls, const std::vector<std::wstring>& activities, bool companions)
        {
            return std::any_of(calls.begin(), calls.end(), [&](const std::wstring& c) { return StoryCall(c) || (companions && SentOn(c)); }) ||
                   std::any_of(activities.begin(), activities.end(), [](const std::wstring& a) { return StoryActivity(a); });
        }

        // ---- the state machines as SMED exports them ----------------------------------------
        //
        //   function Scene03_shootingrange_State_3(sm)
        //     Debug_SetSMEDStateID(sm, 340)
        //     if SF_IsInTrigger(sm, {gameObjects = "Sam",
        //         trigger = "T_a_shootingrange"}) then
        //       M_JumpToState(sm,7)
        //       return
        //     end
        //   end
        //   ...
        //   gRegisteredStatemachines.Scene03_shootingrange = { ... states = {
        //     {nextState = 4, func = Scene03_shootingrange_State_7, funcList = '...'}, ...
        //
        // A state's function runs at every update; a state registered with a next state passes
        // on to it at once unless one of its conditions jumps (an action, or a branch), one without
        // waits until a condition's link jumps. Conditions nest: a question can stand inside
        // another condition's link (the Act2_2 companion's loop asks about leaving the selfie only
        // while the selfie is not done).

        // A condition and what runs when it holds.
        struct Block
        {
            std::wstring condition;           // without its "then"; "else" for an else
            std::wstring text;                // the statements inside, those of nested conditions' links included
            std::vector<struct Block> blocks; // the conditions nested inside
        };

        struct StateText
        {
            std::wstring direct; // the statements outside any condition
            std::vector<Block> blocks;
        };

        struct Machine
        {
            std::map<int, StateText> states;
            std::map<int, int> next; // the registered next state, where it is not nil
        };

        std::wstring Code(const std::wstring& line)
        {
            // The exports keep comments on lines of their own and no "--" inside a string.
            const auto at = line.find(L"--");
            return str::Trim(at == std::wstring::npos ? std::wstring_view(line) : std::wstring_view(line).substr(0, at));
        }

        bool Opens(const std::wstring& s)
        {
            return str::StartsWith(s, L"if ") || str::StartsWith(s, L"for ") || str::StartsWith(s, L"while ") || s == L"do" || s == L"repeat" ||
                   str::StartsWith(s, L"function ") || str::StartsWith(s, L"local function ") || s.find(L"function(") != std::wstring::npos;
        }

        bool Closes(const std::wstring& s)
        {
            return s == L"end" || str::StartsWith(s, L"end ") || str::StartsWith(s, L"end)") || str::StartsWith(s, L"end,") || str::StartsWith(s, L"until ");
        }

        // A line that opens and closes a block of its own ("if x then y end").
        bool Whole(const std::wstring& s)
        {
            return Opens(s) && (str::EndsWith(s, L" end") || str::EndsWith(s, L" end)"));
        }

        std::wstring WithoutThen(std::wstring condition)
        {
            condition = str::Trim(condition);
            if (str::EndsWith(condition, L"then")) condition = str::Trim(std::wstring_view(condition).substr(0, condition.size() - 4));
            return condition;
        }

        // A condition starting at `lines[i]` ("if " or "elseif " cut off as `skip`), over as many
        // lines as it takes to reach its "then": the condition, and the line that ends it.
        std::wstring ConditionAt(const std::vector<std::wstring>& lines, size_t& i, size_t skip)
        {
            std::wstring condition = lines[i].substr(skip);
            while (!str::EndsWith(lines[i], L"then") && i + 1 < lines.size())
            {
                ++i;
                condition += L" " + lines[i];
            }
            return WithoutThen(condition);
        }

        // Reads a body from `i` up to the line that closes it at its own depth ("end", "elseif",
        // "else"), and returns that line's index.
        size_t ReadBody(const std::vector<std::wstring>& lines, size_t i, std::wstring* direct, std::wstring& text, std::vector<Block>& blocks)
        {
            for (; i < lines.size(); ++i)
            {
                const auto& s = lines[i];
                if (s.empty()) continue;
                if (Closes(s) || str::StartsWith(s, L"elseif ") || s == L"else") return i;
                if (str::StartsWith(s, L"if ") && !Whole(s))
                {
                    Block block;
                    block.condition = ConditionAt(lines, i, 3);
                    size_t at = ReadBody(lines, i + 1, nullptr, block.text, block.blocks);
                    text += block.text;
                    // An "elseif" or an "else" goes on the same "end".
                    while (at < lines.size() && (str::StartsWith(lines[at], L"elseif ") || lines[at] == L"else"))
                    {
                        blocks.push_back(std::move(block));
                        block = Block{};
                        if (lines[at] == L"else")
                            block.condition = L"else";
                        else
                            block.condition = ConditionAt(lines, at, 7);
                        at = ReadBody(lines, at + 1, nullptr, block.text, block.blocks);
                        text += block.text;
                    }
                    blocks.push_back(std::move(block));
                    i = at;
                    continue;
                }
                if (Opens(s) && !Whole(s))
                {
                    // A loop or a function inside: its statements are this body's, its conditions
                    // are read like any other.
                    if (direct) *direct += s + L"\n";
                    text += s + L"\n";
                    std::wstring inner;
                    i = ReadBody(lines, i + 1, nullptr, inner, blocks);
                    if (direct) *direct += inner;
                    text += inner;
                    continue;
                }
                if (direct) *direct += s + L"\n";
                text += s + L"\n";
            }
            return i;
        }

        std::map<std::wstring, Machine> ParseMachines(const std::wstring& script)
        {
            std::map<std::wstring, Machine> machines;
            std::vector<std::wstring> lines;
            for (auto& line : str::Split(script, L'\n', false))
            {
                if (!line.empty() && line.back() == L'\r') line.pop_back();
                lines.push_back(Code(line));
            }
            static const std::wregex header(LR"re(^function\s+([A-Za-z0-9_]+)_State_(\d+)\s*\(\s*sm\s*\))re");
            for (size_t i = 0; i < lines.size(); ++i)
            {
                if (!str::StartsWith(lines[i], L"function ")) continue;
                std::wsmatch m;
                if (!std::regex_search(lines[i], m, header)) continue;
                StateText state;
                std::wstring all;
                i = ReadBody(lines, i + 1, &state.direct, all, state.blocks);
                machines[m[1].str()].states[std::stoi(m[2].str())] = std::move(state);
            }
            static const std::wregex registered(LR"re(\{\s*nextState\s*=\s*(\d+|nil)\s*,\s*func\s*=\s*([A-Za-z0-9_]+)_State_(\d+))re");
            for (auto it = std::wsregex_iterator(script.begin(), script.end(), registered); it != std::wsregex_iterator(); ++it)
            {
                const auto next = (*it)[1].str();
                if (next == L"nil") continue;
                machines[(*it)[2].str()].next[std::stoi((*it)[3].str())] = std::stoi(next);
            }
            return machines;
        }

        std::vector<int> JumpsIn(const std::wstring& text)
        {
            static const std::wregex jump(LR"re(M_JumpToState\s*\(\s*sm\s*,\s*(\d+)\s*\))re");
            std::vector<int> jumps;
            for (auto it = std::wsregex_iterator(text.begin(), text.end(), jump); it != std::wsregex_iterator(); ++it)
                jumps.push_back(std::stoi((*it)[1].str()));
            return jumps;
        }

        // A condition that holds by itself once its time is up.
        bool TimeWait(const std::wstring& condition)
        {
            return str::StartsWith(condition, L"SF_Wait(") || str::StartsWith(condition, L"SF_Wait (");
        }

        // A state that waits for its time alone: its one condition is a wait for time.
        bool WaitsForTime(const StateText& text)
        {
            return text.blocks.size() == 1 && TimeWait(text.blocks.front().condition);
        }

        struct Question
        {
            bool negated = false;
            std::wstring who;
            std::vector<std::wstring> triggers;
        };

        std::vector<Question> QuestionsIn(const std::wstring& condition)
        {
            static const std::wregex poll(LR"re((not\s+)?SF_IsInTrigger\s*\(\s*sm\s*,\s*\{([^}]*)\}\s*\))re");
            static const std::wregex who(LR"re(gameObjects\s*=\s*"([^"]*)")re");
            static const std::wregex trigger(LR"re(trigger\s*=\s*"([^"]*)")re");
            std::vector<Question> questions;
            if (condition.find(L"SF_IsInTrigger") == std::wstring::npos) return questions;
            for (auto it = std::wsregex_iterator(condition.begin(), condition.end(), poll); it != std::wsregex_iterator(); ++it)
            {
                Question q;
                q.negated = (*it)[1].matched;
                const auto args = (*it)[2].str();
                std::wsmatch m;
                if (std::regex_search(args, m, who)) q.who = str::Trim(m[1].str());
                if (std::regex_search(args, m, trigger))
                {
                    for (const auto& one : str::Split(m[1].str(), L','))
                    {
                        if (!one.empty()) q.triggers.push_back(one);
                    }
                }
                if (!q.triggers.empty()) questions.push_back(std::move(q));
            }
            return questions;
        }

        // Where a link leads: the calls and activities on the way, and the waiting states the
        // machine comes to rest in.
        struct Path
        {
            std::vector<std::wstring> calls;
            std::vector<std::wstring> activities;
            std::vector<int> waits;
            bool enables = false;             // turns on an activity (SF_SetEnableActivity with enable = true)
            std::vector<std::wstring> sentOn; // the companions sent on ahead (SentOn calls' "character")
        };

        // The companions a text sends on ahead: the "character" of each SF_OrderLeadTo and
        // SF_OrderMoveTo.
        std::vector<std::wstring> SentOnIn(const std::wstring& text)
        {
            static const std::wregex call(LR"re((SF_[A-Za-z0-9_]+)\s*\(\s*sm\s*,\s*\{([^}]*)\})re");
            static const std::wregex character(LR"re(character\s*=\s*"([^"]*)")re");
            std::vector<std::wstring> sent;
            for (auto it = std::wsregex_iterator(text.begin(), text.end(), call); it != std::wsregex_iterator(); ++it)
            {
                if (!SentOn((*it)[1].str())) continue;
                const auto args = (*it)[2].str();
                std::wsmatch m;
                if (!std::regex_search(args, m, character)) continue;
                const auto name = str::Trim(m[1].str());
                if (!name.empty() && std::find(sent.begin(), sent.end(), name) == sent.end()) sent.emplace_back(name);
            }
            return sent;
        }

        // Whether a text turns an activity on for the player to use.
        bool EnablesIn(const std::wstring& text)
        {
            static const std::wregex call(LR"re(SF_SetEnableActivity\s*\(\s*sm\s*,\s*\{([^}]*)\})re");
            static const std::wregex on(LR"re(enable\s*=\s*true)re");
            for (auto it = std::wsregex_iterator(text.begin(), text.end(), call); it != std::wsregex_iterator(); ++it)
            {
                const auto args = (*it)[1].str();
                if (std::regex_search(args, on)) return true;
            }
            return false;
        }

        void Take(Path& path, const std::wstring& text)
        {
            AddUnique(path.calls, CallsIn(text));
            for (const auto& a : ActivitiesIn(text))
            {
                if (std::find(path.activities.begin(), path.activities.end(), a) == path.activities.end()) path.activities.push_back(a);
            }
            if (EnablesIn(text)) path.enables = true;
            for (const auto& c : SentOnIn(text))
            {
                if (std::find(path.sentOn.begin(), path.sentOn.end(), c) == path.sentOn.end()) path.sentOn.push_back(c);
            }
        }

        void WalkFrom(const Machine& machine, int state, Path& path, std::set<int>& visited)
        {
            for (int steps = 0; steps < 64; ++steps)
            {
                if (!visited.insert(state).second) return;
                const auto found = machine.states.find(state);
                if (found == machine.states.end()) return;
                const StateText& text = found->second;
                Take(path, text.direct);
                const auto next = machine.next.find(state);
                // An action, or a branch that passes on when none of its conditions holds.
                if (text.blocks.empty() || next != machine.next.end())
                {
                    if (next == machine.next.end()) return;
                    state = next->second;
                    continue;
                }
                // A wait for time passes by itself.
                if (WaitsForTime(text))
                {
                    Take(path, text.blocks.front().text);
                    const auto jumps = JumpsIn(text.blocks.front().text);
                    if (jumps.empty()) return;
                    state = jumps.back();
                    continue;
                }
                path.waits.push_back(state);
                return;
            }
        }

        // Everything a machine can do from `start`, every condition taken either way; or, given
        // the variables, a comparison that does not hold now leaves its link out, and one that does
        // and jumps takes the machine past its registered next state (M_JumpToState puts its own
        // in place of it).
        void Reach(const Machine& machine, int start, Path& path, const Variables* variables = nullptr)
        {
            std::vector<int> todo{start};
            std::set<int> seen;
            while (!todo.empty())
            {
                const int state = todo.back();
                todo.pop_back();
                if (!seen.insert(state).second) continue;
                const auto found = machine.states.find(state);
                if (found == machine.states.end()) continue;
                Take(path, found->second.direct);
                bool jumpsSurely = false;
                for (const auto& block : found->second.blocks)
                {
                    const auto holds = variables ? Holds(block.condition, *variables) : std::nullopt;
                    if (holds == false) continue;
                    Take(path, block.text);
                    const auto jumps = JumpsIn(block.text);
                    for (const int jump : jumps)
                        todo.push_back(jump);
                    if (holds == true && !jumps.empty()) jumpsSurely = true;
                }
                if (const auto next = machine.next.find(state); next != machine.next.end() && !jumpsSurely) todo.push_back(next->second);
            }
        }

        // The variables a script's comparisons read.
        std::vector<std::wstring> ComparedVariables(const std::wstring& script)
        {
            static const std::wregex read(LR"re(number[12]\s*=\s*gVariables\.([A-Za-z0-9_]+))re");
            std::vector<std::wstring> names;
            for (auto it = std::wsregex_iterator(script.begin(), script.end(), read); it != std::wsregex_iterator(); ++it)
            {
                const auto name = (*it)[1].str();
                if (std::find(names.begin(), names.end(), name) == names.end()) names.push_back(name);
            }
            return names;
        }

        Path Follow(const Machine& machine, int from, const std::wstring& link)
        {
            Path path;
            Take(path, link);
            for (int target : JumpsIn(link))
            {
                std::set<int> seen{from};
                WalkFrom(machine, target, path, seen);
            }
            return path;
        }

        // Whether every state the way on from a question comes to rest in leads straight back to that
        // question, moving nothing of the story on the way: a gag the scene plays each time she walks
        // in and then waits for again. The library's security camera (act3_11's
        // Scene02_spookylibrary_cctv) asks whether she is in T_Cam_Library_50, plays the camera's
        // look, waits for her to leave it, turns the look off and asks again; a tarantula is started
        // on the way only when Hill's spider was chosen. It stood listed as a way on through the rest
        // of the level, said reached and never gone (2026-11-06).
        bool ComesBack(const Machine& machine, int question, const Path& way)
        {
            if (way.waits.empty()) return false;
            for (const int rest : way.waits)
            {
                if (rest == question) continue;
                const auto found = machine.states.find(rest);
                if (found == machine.states.end() || found->second.blocks.empty()) return false;
                for (const auto& block : found->second.blocks)
                {
                    if (JumpsIn(block.text).empty()) return false;
                    const Path back = Follow(machine, rest, block.text);
                    if (MovesOn(back.calls, back.activities, true)) return false;
                    if (back.waits.empty() || std::any_of(back.waits.begin(), back.waits.end(), [&](int w) { return w != question; })) return false;
                }
            }
            return true;
        }

        // Whether a state registered with a next state comes back to itself on its own: the
        // companion's loop in Act2_2 passes from its questions to a wait of a tenth of a second
        // and back, so it asks at every turn; the answerphone's single look in act2_11 passes on
        // and never comes back.
        bool Loops(const Machine& machine, int state)
        {
            const auto first = machine.next.find(state);
            if (first == machine.next.end()) return false;
            int at = first->second;
            std::set<int> visited;
            for (int steps = 0; steps < 64; ++steps)
            {
                if (at == state) return true;
                if (!visited.insert(at).second) return false;
                const auto found = machine.states.find(at);
                if (found == machine.states.end()) return false;
                const auto next = machine.next.find(at);
                if (found->second.blocks.empty() || next != machine.next.end())
                {
                    if (next == machine.next.end()) return false;
                    at = next->second;
                    continue;
                }
                if (!WaitsForTime(found->second)) return false;
                const auto jumps = JumpsIn(found->second.blocks.front().text);
                if (jumps.empty()) return false;
                at = jumps.back();
            }
            return false;
        }

        bool SameCharacter(const std::wstring& a, const std::wstring& b)
        {
            return str::EqualsNoCase(a, b) || str::EqualsNoCase(a, L"Player") || str::EqualsNoCase(b, L"Player");
        }

        // Every question in a state's conditions, at any depth, with the link it guards.
        void Questions(const std::vector<Block>& blocks, std::vector<std::pair<const Block*, Question>>& out)
        {
            for (const auto& block : blocks)
            {
                for (auto& q : QuestionsIn(block.condition))
                    out.emplace_back(&block, std::move(q));
                Questions(block.blocks, out);
            }
        }
    }

    struct Script
    {
        std::map<std::wstring, Machine> machines;
        int start = 1;
    };

    bool StoryCall(std::wstring_view call)
    {
        return std::find(std::begin(kStoryCalls), std::end(kStoryCalls), call) != std::end(kStoryCalls);
    }

    std::optional<bool> Compare(std::wstring_view operation, const Number& a, const Number& b)
    {
        using Kind = Number::Kind;
        if (a.kind == Kind::Unknown || b.kind == Kind::Unknown) return std::nullopt;
        const bool known = operation == L"Equal" || operation == L"NotEqual" || operation == L"GreaterThan" || operation == L"GreaterThanOrEqual" ||
                           operation == L"LessThan" || operation == L"LessThanOrEqual";
        if (!known) return std::nullopt;
        // Misc.lua's Compare: a nil on the left equals only a nil, and orders nothing; a nil on the
        // right differs from a number and orders nothing.
        if (a.kind == Kind::Nil)
        {
            if (operation == L"NotEqual") return b.kind != Kind::Nil;
            if (operation.find(L"Equal") != std::wstring_view::npos) return b.kind == Kind::Nil;
            return false;
        }
        if (b.kind == Kind::Nil) return operation == L"NotEqual";
        if (operation == L"Equal") return a.value == b.value;
        if (operation == L"NotEqual") return a.value != b.value;
        if (operation == L"GreaterThan") return a.value > b.value;
        if (operation == L"GreaterThanOrEqual") return a.value >= b.value;
        if (operation == L"LessThan") return a.value < b.value;
        return a.value <= b.value;
    }

    std::optional<bool> Holds(const std::wstring& condition, const Variables& variables)
    {
        static const std::wregex call(LR"re(^(not\s+)?SF_Comparison\s*\(\s*sm\s*,\s*\{([^}]*)\}\s*\)$)re");
        static const std::wregex type(LR"re(type\s*=\s*"([^"]*)")re");
        static const std::wregex operation(LR"re(operation\s*=\s*"([^"]*)")re");
        static const std::wregex operand1(LR"re(number1\s*=\s*([^,}\s]+))re");
        static const std::wregex operand2(LR"re(number2\s*=\s*([^,}\s]+))re");
        static const std::wregex literal(LR"re(^-?[0-9]+(?:\.[0-9]+)?$)re");
        const auto text = str::Trim(condition);
        std::wsmatch m;
        if (!std::regex_match(text, m, call)) return std::nullopt;
        const bool negated = m[1].matched;
        const auto args = m[2].str();
        std::wsmatch t, op, n1, n2;
        if (!std::regex_search(args, t, type) || t[1].str() != L"Number") return std::nullopt;
        if (!std::regex_search(args, op, operation) || !std::regex_search(args, n1, operand1) || !std::regex_search(args, n2, operand2)) return std::nullopt;
        const auto operand = [&](const std::wstring& token) -> Number
        {
            if (std::regex_match(token, literal)) return Number{Number::Kind::Value, std::stod(token)};
            if (str::StartsWith(token, L"gVariables.") && variables) return variables(token.substr(11));
            return Number{};
        };
        const auto holds = Compare(op[1].str(), operand(n1[1].str()), operand(n2[1].str()));
        if (!holds) return std::nullopt;
        return negated ? !*holds : *holds;
    }

    bool StoryActivity(std::wstring_view activity)
    {
        return !activity.empty() && !str::StartsWith(activity, L"PAO_") && !str::StartsWith(activity, L"pao_");
    }

    bool StoryActor(std::wstring_view coreName, bool background, bool performance)
    {
        return !background && !performance && (coreName.empty() || StoryActivity(coreName));
    }

    bool ParseEnterEvent(const std::wstring& script, EnterEvent& out)
    {
        static const std::wregex header(LR"re(gEvents\["EnterTrigger"\]\["([^"]+)"\])re");
        static const std::wregex limit(LR"re(triggerLimit\s*=\s*(-?\d+))re");
        std::wsmatch m;
        if (!std::regex_search(script, m, header)) return false;
        out.trigger = m[1].str();
        if (std::regex_search(script, m, limit)) out.limit = std::stoi(m[1].str());
        // What walking in does is what the script's machine reaches from its start, every
        // condition taken either way: a state nothing leads to is no part of it (the totem
        // plaque's trigger at chapter one still holds the start of a look its designers cut off).
        Path path;
        auto machines = ParseMachines(script);
        if (machines.empty())
        {
            Take(path, script);
        }
        else
        {
            static const std::wregex startState(LR"re(startState\s*=\s*(\d+))re");
            const int start = std::regex_search(script, m, startState) ? std::stoi(m[1].str()) : 1;
            for (const auto& [name, machine] : machines)
                Reach(machine, start, path);
            auto kept = std::make_shared<Script>();
            kept->machines = std::move(machines);
            kept->start = start;
            out.script = std::move(kept);
            out.variables = ComparedVariables(script);
        }
        for (const auto& name : path.calls)
        {
            if (name != L"SF_End") out.calls.push_back(name);
        }
        out.activities = path.activities;
        // A companion's order here is not the scene's: a trigger's script runs whenever she walks
        // in (most of these every time), and progress never waits on it.
        out.story = MovesOn(out.calls, out.activities, false);
        return true;
    }

    bool StoryNow(const EnterEvent& event, const Variables& variables)
    {
        if (!event.story || !event.script || event.variables.empty()) return event.story;
        Path path;
        for (const auto& [name, machine] : event.script->machines)
            Reach(machine, event.script->start, path, &variables);
        std::vector<std::wstring> calls;
        for (const auto& name : path.calls)
        {
            if (name != L"SF_End") calls.push_back(name);
        }
        return MovesOn(calls, path.activities, false);
    }

    std::vector<ActivitySwitch> ParseActivitySwitches(const std::wstring& script)
    {
        static const std::wregex header(LR"re(gEvents\["(EnterTrigger|LeaveTrigger)"\]\["([^"]+)"\])re");
        static const std::wregex call(LR"re(SF_SetEnableActivity\s*\(\s*sm\s*,\s*\{([^}]*)\})re");
        static const std::wregex activity(LR"re(activit(?:y|ies)\s*=\s*"([^"]*)")re");
        static const std::wregex on(LR"re(enable\s*=\s*true)re");
        std::vector<ActivitySwitch> switches;
        std::wsmatch m;
        if (!std::regex_search(script, m, header)) return switches;
        const bool leave = m[1].str() == L"LeaveTrigger";
        const std::wstring trigger = m[2].str();
        for (auto it = std::wsregex_iterator(script.begin(), script.end(), call); it != std::wsregex_iterator(); ++it)
        {
            const auto args = (*it)[1].str();
            std::wsmatch a;
            if (!std::regex_search(args, a, activity)) continue;
            const bool enable = std::regex_search(args, on);
            for (const auto& one : str::Split(a[1].str(), L','))
            {
                const auto name = str::Trim(one);
                if (!name.empty()) switches.push_back(ActivitySwitch{trigger, leave, std::wstring(name), enable});
            }
        }
        return switches;
    }

    std::map<std::wstring, std::vector<std::wstring>> OffInside(const std::vector<ActivitySwitch>& switches)
    {
        std::map<std::wstring, std::vector<std::wstring>> off;
        for (const auto& in : switches)
        {
            if (in.leave || in.enable) continue;
            const bool back = std::any_of(switches.begin(), switches.end(), [&](const ActivitySwitch& out)
                                          { return out.leave && out.enable && out.trigger == in.trigger && out.activity == in.activity; });
            if (!back) continue;
            auto& volumes = off[in.activity];
            if (std::find(volumes.begin(), volumes.end(), in.trigger) == volumes.end()) volumes.push_back(in.trigger);
        }
        return off;
    }

    bool EndsObjective(const Poll& poll)
    {
        return std::any_of(poll.calls.begin(), poll.calls.end(),
                           [](const std::wstring& call) { return call == L"SF_TurnOffCurrentObjective" || call == L"SF_SetCurrentObjective"; });
    }

    std::vector<Poll> ParsePolls(const std::wstring& script, const std::wstring& source)
    {
        std::vector<Poll> polls;
        if (script.find(L"SF_IsInTrigger") == std::wstring::npos) return polls;
        const auto machines = ParseMachines(script);
        // Each question with the waiting states its way on comes to rest in.
        std::vector<std::vector<int>> restsIn;
        for (const auto& [name, machine] : machines)
        {
            for (const auto& [number, state] : machine.states)
            {
                std::vector<std::pair<const Block*, Question>> questions;
                Questions(state.blocks, questions);
                if (questions.empty()) continue;
                const bool waits = !machine.next.count(number) || Loops(machine, number);
                for (const auto& [block, q] : questions)
                {
                    const Path path = Follow(machine, number, block->text);
                    const bool leaves = !JumpsIn(block->text).empty();
                    for (const auto& trigger : q.triggers)
                    {
                        Poll p;
                        p.source = source;
                        p.machine = name;
                        p.state = number;
                        p.trigger = trigger;
                        p.who = q.who;
                        p.negated = q.negated;
                        p.waits = waits;
                        p.leaves = leaves;
                        p.calls = path.calls;
                        p.activities = path.activities;
                        p.ends = std::find(path.calls.begin(), path.calls.end(), L"SF_End") != path.calls.end();
                        // Turning on a thing to use once she stands there moves the scene on too: she can
                        // only bring it about by standing there.
                        const bool alone = MovesOn(path.calls, path.activities, false) || path.enables;
                        p.story = !q.negated && waits && leaves && (alone || MovesOn(path.calls, path.activities, true)) &&
                                  !ComesBack(machine, number, path);
                        p.sentOnly = p.story && !alone;
                        if (p.sentOnly) p.sentOn = path.sentOn;
                        polls.push_back(std::move(p));
                        restsIn.push_back(path.waits);
                    }
                }
            }
        }
        // A way on that comes to rest in a state the scene keeps only while she stands there (it
        // asks there whether she has left the volume, and goes back if she has) moves the scene on as
        // that state's other ways on do: act4_3's Scene03_look_for_map asks in its state 12 whether
        // Matt stands in T_Cam_station_Interior_20b by the ladder, and in its state 11, while he
        // does, waits for Emily to pick up her torch and turns on the climb down. The volume was
        // judged nothing of the story's, and Matt stood about the station with nothing to walk to
        // (2026-11-07).
        for (size_t i = 0; i < polls.size(); ++i)
        {
            Poll& p = polls[i];
            if ((p.story && !p.sentOnly) || p.negated || !p.waits || !p.leaves) continue;
            const auto machine = machines.find(p.machine);
            if (machine == machines.end()) continue;
            const auto asks = [&](const Question& q) { return std::find(q.triggers.begin(), q.triggers.end(), p.trigger) != q.triggers.end(); };
            for (const int rest : restsIn[i])
            {
                if (rest == p.state || (p.story && !p.sentOnly)) continue;
                const auto found = machine->second.states.find(rest);
                if (found == machine->second.states.end()) continue;
                std::vector<std::pair<const Block*, Question>> asked;
                Questions(found->second.blocks, asked);
                const bool kept =
                    std::any_of(asked.begin(), asked.end(), [&](const auto& a) { return a.second.negated && SameCharacter(a.second.who, p.who) && asks(a.second); });
                if (!kept) continue;
                for (const auto& block : found->second.blocks)
                {
                    const auto questions = QuestionsIn(block.condition);
                    if (std::any_of(questions.begin(), questions.end(), asks) || JumpsIn(block.text).empty()) continue;
                    const Path on = Follow(machine->second, rest, block.text);
                    if (MovesOn(on.calls, on.activities, false) || on.enables)
                    {
                        p.story = true;
                        p.sentOnly = false;
                        p.sentOn.clear();
                        break;
                    }
                    if (MovesOn(on.calls, on.activities, true))
                    {
                        p.story = true;
                        p.sentOnly = true;
                        for (const auto& c : on.sentOn)
                        {
                            if (std::find(p.sentOn.begin(), p.sentOn.end(), c) == p.sentOn.end()) p.sentOn.push_back(c);
                        }
                    }
                }
            }
        }
        // A way on that leads only to another question for the same character moves the scene
        // on as that one does.
        for (bool changed = true; changed;)
        {
            changed = false;
            for (size_t i = 0; i < polls.size(); ++i)
            {
                Poll& p = polls[i];
                if ((p.story && !p.sentOnly) || p.negated || !p.waits || !p.leaves) continue;
                for (size_t k = 0; k < polls.size() && !(p.story && !p.sentOnly); ++k)
                {
                    const Poll& other = polls[k];
                    if (k == i || !other.story || other.machine != p.machine || !SameCharacter(other.who, p.who)) continue;
                    if (std::find(restsIn[i].begin(), restsIn[i].end(), other.state) == restsIn[i].end()) continue;
                    // As that question moves the scene on: by itself, or only by the companions it
                    // sends on.
                    if (!other.sentOnly)
                    {
                        p.story = true;
                        p.sentOnly = false;
                        p.sentOn.clear();
                        changed = true;
                        continue;
                    }
                    if (!p.story)
                    {
                        p.story = true;
                        p.sentOnly = true;
                        changed = true;
                    }
                    for (const auto& c : other.sentOn)
                    {
                        if (std::find(p.sentOn.begin(), p.sentOn.end(), c) != p.sentOn.end()) continue;
                        p.sentOn.push_back(c);
                        changed = true;
                    }
                }
            }
        }
        return polls;
    }

    std::vector<EndWait> ParseEndWaits(const std::wstring& script, const std::wstring& source)
    {
        std::vector<EndWait> waits;
        if (script.find(L"SF_IsStatemachineDone") == std::wstring::npos) return waits;
        static const std::wregex ask(LR"re((not\s+)?SF_IsStatemachineDone\s*\(\s*sm\s*,\s*\{([^}]*)\}\s*\))re");
        static const std::wregex awaited(LR"re(statemachine\s*=\s*"([^"]*)")re");
        const auto machines = ParseMachines(script);
        for (const auto& [name, machine] : machines)
        {
            for (const auto& [number, state] : machine.states)
            {
                const bool waitsHere = !machine.next.count(number) || Loops(machine, number);
                if (!waitsHere) continue;
                const auto visit = [&](const auto& self, const std::vector<Block>& blocks) -> void
                {
                    for (const auto& block : blocks)
                    {
                        for (auto it = std::wsregex_iterator(block.condition.begin(), block.condition.end(), ask); it != std::wsregex_iterator(); ++it)
                        {
                            if ((*it)[1].matched || JumpsIn(block.text).empty()) continue;
                            const std::wstring args = (*it)[2].str();
                            std::wsmatch m;
                            if (!std::regex_search(args, m, awaited)) continue;
                            EndWait w;
                            w.source = source;
                            w.machine = name;
                            w.state = number;
                            w.awaited = m[1].str();
                            const Path on = Follow(machine, number, block.text);
                            w.story = MovesOn(on.calls, on.activities, false) ||
                                      std::find(on.calls.begin(), on.calls.end(), L"SF_RunStatemachine") != on.calls.end();
                            waits.push_back(std::move(w));
                        }
                        self(self, block.blocks);
                    }
                };
                visit(visit, state.blocks);
            }
        }
        return waits;
    }

    namespace
    {
        // The orders a text gives companions (SF_OrderLeadTo, SF_OrderEscort, SF_OrderForceIdle,
        // SF_OrderMoveTo, ...), in order: the companion, and whether it is a lead of the player.
        std::vector<std::pair<std::wstring, bool>> OrdersIn(const std::wstring& text)
        {
            static const std::wregex call(LR"re((SF_Order[A-Za-z]+)\s*\(\s*sm\s*,\s*\{([^}]*)\})re");
            static const std::wregex character(LR"re(character\s*=\s*"([^"]*)")re");
            static const std::wregex player(LR"re(lead\s*=\s*"Player")re");
            std::vector<std::pair<std::wstring, bool>> orders;
            for (auto it = std::wsregex_iterator(text.begin(), text.end(), call); it != std::wsregex_iterator(); ++it)
            {
                const std::wstring args = (*it)[2].str();
                std::wsmatch m;
                if (!std::regex_search(args, m, character)) continue;
                orders.emplace_back(m[1].str(), (*it)[1].str() == L"SF_OrderLeadTo" && std::regex_search(args, player));
            }
            return orders;
        }
    }

    std::vector<Leading> ParseLeads(const std::wstring& script, const std::wstring& source)
    {
        std::vector<Leading> leads;
        if (script.find(L"SF_OrderLeadTo") == std::wstring::npos) return leads;
        const auto machines = ParseMachines(script);
        for (const auto& [name, machine] : machines)
        {
            // Each state's last order to each companion, and the ways into each state.
            std::map<int, std::map<std::wstring, bool>> orders;
            std::map<int, std::vector<int>> into;
            std::set<std::wstring> companions;
            for (const auto& [number, state] : machine.states)
            {
                std::wstring text = state.direct;
                for (const auto& block : state.blocks)
                    text += L"\n" + block.text;
                for (const auto& [who, leadsPlayer] : OrdersIn(text))
                {
                    orders[number][who] = leadsPlayer;
                    companions.insert(who);
                }
                if (const auto next = machine.next.find(number); next != machine.next.end()) into[next->second].push_back(number);
                for (const auto& block : state.blocks)
                {
                    for (const int target : JumpsIn(block.text))
                        into[target].push_back(number);
                }
            }
            for (const auto& [number, state] : machine.states)
            {
                const bool waits = !machine.next.count(number) || Loops(machine, number);
                if (!waits || state.blocks.empty()) continue;
                for (const auto& who : companions)
                {
                    // Back along every way in to the last order to her: a lead on each, no other order, and
                    // no way in from the machine's start with none.
                    bool led = false;
                    bool other = false;
                    std::set<int> seen{number};
                    std::vector<int> back = into[number];
                    if (back.empty()) other = true;
                    while (!back.empty() && !other)
                    {
                        const int at = back.back();
                        back.pop_back();
                        if (!seen.insert(at).second) continue;
                        if (const auto given = orders.find(at); given != orders.end())
                        {
                            if (const auto order = given->second.find(who); order != given->second.end())
                            {
                                (order->second ? led : other) = true;
                                continue;
                            }
                        }
                        const auto& before = into[at];
                        if (before.empty()) other = true;
                        back.insert(back.end(), before.begin(), before.end());
                    }
                    if (led && !other) leads.push_back(Leading{source, name, number, who});
                }
            }
        }
        return leads;
    }

    std::set<std::wstring> LeadingNow(const std::vector<Leading>& leads, const std::vector<std::pair<std::wstring, int>>& machines)
    {
        std::set<std::wstring> now;
        for (const auto& lead : leads)
        {
            for (const auto& [name, state] : machines)
            {
                if (name == lead.machine && state == lead.state) now.insert(lead.character);
            }
        }
        return now;
    }

    size_t WeighEnds(std::vector<Poll>& polls, const std::vector<EndWait>& ends)
    {
        size_t moved = 0;
        for (auto& p : polls)
        {
            if ((p.story && !p.sentOnly) || !p.ends || p.negated || !p.waits || !p.leaves) continue;
            const bool awaited = std::any_of(ends.begin(), ends.end(), [&](const EndWait& w) { return w.story && str::EqualsNoCase(w.awaited, p.machine); });
            if (!awaited) continue;
            p.story = true;
            p.sentOnly = false;
            p.sentOn.clear();
            ++moved;
        }
        return moved;
    }

    size_t WeighCompanions(std::vector<Poll>& polls)
    {
        std::vector<std::wstring> awaited;
        for (const auto& p : polls)
        {
            if (!p.story || p.sentOnly || p.who.empty() || str::EqualsNoCase(p.who, L"Player")) continue;
            if (std::none_of(awaited.begin(), awaited.end(), [&](const std::wstring& a) { return str::EqualsNoCase(a, p.who); })) awaited.push_back(p.who);
        }
        size_t demoted = 0;
        for (auto& p : polls)
        {
            if (!p.story || !p.sentOnly) continue;
            const bool waitedFor = std::any_of(p.sentOn.begin(), p.sentOn.end(),
                                               [&](const std::wstring& c)
                                               { return std::any_of(awaited.begin(), awaited.end(), [&](const std::wstring& a) { return str::EqualsNoCase(a, c); }); });
            if (waitedFor) continue;
            p.story = false;
            ++demoted;
        }
        return demoted;
    }

    std::vector<MoveWait> ParseMoveWaits(const std::wstring& script, const std::wstring& source)
    {
        std::vector<MoveWait> waits;
        if (script.find(L"SF_IsCharacterMoving") == std::wstring::npos) return waits;
        static const std::wregex ask(LR"re((not\s+)?SF_IsCharacterMoving\s*\(\s*sm\s*,\s*\{([^}]*)\}\s*\))re");
        static const std::wregex character(LR"re(character\s*=\s*"([^"]*)")re");
        const auto machines = ParseMachines(script);
        for (const auto& [name, machine] : machines)
        {
            for (const auto& [number, state] : machine.states)
            {
                const bool waitsHere = !machine.next.count(number) || Loops(machine, number);
                const auto visit = [&](const auto& self, const std::vector<Block>& blocks) -> void
                {
                    for (const auto& block : blocks)
                    {
                        for (auto it = std::wsregex_iterator(block.condition.begin(), block.condition.end(), ask); it != std::wsregex_iterator(); ++it)
                        {
                            MoveWait w;
                            w.source = source;
                            w.machine = name;
                            w.state = number;
                            w.negated = (*it)[1].matched;
                            const auto args = (*it)[2].str();
                            std::wsmatch m;
                            if (std::regex_search(args, m, character)) w.who = str::Trim(m[1].str());
                            w.waits = waitsHere;
                            const Path path = Follow(machine, number, block.text);
                            w.story = !w.negated && waitsHere && !JumpsIn(block.text).empty() && MovesOn(path.calls, path.activities, true) &&
                                      !ComesBack(machine, number, path);
                            waits.push_back(std::move(w));
                        }
                        self(self, block.blocks);
                    }
                };
                visit(visit, state.blocks);
            }
        }
        return waits;
    }

    bool WaitsToMove(const std::vector<MoveWait>& waits, const std::vector<std::pair<std::wstring, int>>& machines, const std::wstring& character)
    {
        for (const auto& w : waits)
        {
            if (!w.story || !(str::EqualsNoCase(w.who, character) || str::EqualsNoCase(w.who, L"Player"))) continue;
            for (const auto& [machine, state] : machines)
            {
                if (machine == w.machine && state == w.state) return true;
            }
        }
        return false;
    }

    void Waits::SetPolls(std::vector<Poll> polls)
    {
        m_polls = std::move(polls);
    }

    void Waits::Asked(double now, const std::wstring& trigger, bool player)
    {
        Asking& a = m_asked[trigger];
        a.any = now;
        if (player) a.player = now;
    }

    void Waits::Answered(double now, const std::wstring& trigger, bool yes)
    {
        if (yes) m_asked[trigger].yes = now;
    }

    bool Waits::AskedNow(double now, const std::wstring& trigger, bool player) const
    {
        const auto found = m_asked.find(trigger);
        if (found == m_asked.end()) return false;
        return now - (player ? found->second.player : found->second.any) <= kAskedWithin;
    }

    bool Waits::CompanionAwaited(double now, const Poll& poll, const std::wstring& character) const
    {
        for (const auto& companion : poll.sentOn)
        {
            if (str::EqualsNoCase(companion, character) || str::EqualsNoCase(companion, L"Player")) continue;
            for (const auto& wait : m_polls)
            {
                if (!wait.story || wait.sentOnly || !str::EqualsNoCase(wait.who, companion)) continue;
                if (AskedNow(now, wait.trigger, false) && !ElsewhereNow(now, wait)) return true;
            }
        }
        return false;
    }

    bool Waits::ElsewhereNow(double now, const Poll& poll) const
    {
        for (const auto& other : m_polls)
        {
            if (other.source != poll.source || other.machine != poll.machine || other.state == poll.state) continue;
            if (!AskedNow(now, other.trigger, false)) continue;
            // Only that state of all asks about its volume.
            const bool alone =
                std::all_of(m_polls.begin(), m_polls.end(), [&](const Poll& p)
                            { return p.trigger != other.trigger || (p.source == other.source && p.machine == other.machine && p.state == other.state); });
            if (alone) return true;
        }
        return false;
    }

    Waits::Verdict Waits::Judge(double now, const std::wstring& trigger, const std::wstring& character,
                                const std::vector<std::pair<std::wstring, int>>* machines, const std::set<std::wstring>* leading) const
    {
        // A machine the game's own record places elsewhere does not wait in this state now.
        const auto placed = [&](const Poll& p)
        {
            bool listed = false;
            bool there = false;
            if (machines)
            {
                for (const auto& [name, state] : *machines)
                {
                    if (name != p.machine) continue;
                    listed = true;
                    there = there || state == p.state;
                }
            }
            return listed ? there : !ElsewhereNow(now, p);
        };
        // A scene's wait for a companion leading the player on (LeadingNow): asked about her now, it is the
        // player's way on too.
        std::vector<const Poll*> led;
        if (leading)
        {
            for (const auto& p : m_polls)
            {
                if (p.trigger != trigger || !p.story || p.sentOnly || p.negated || !p.waits || !p.leaves) continue;
                if (str::EqualsNoCase(p.who, L"Player") || str::EqualsNoCase(p.who, character)) continue;
                if (std::any_of(leading->begin(), leading->end(), [&](const std::wstring& c) { return str::EqualsNoCase(c, p.who); })) led.push_back(&p);
            }
        }
        const auto ledWaiting = [&](Verdict& v)
        {
            if (led.empty() || !AskedNow(now, trigger, false)) return false;
            for (const Poll* p : led)
            {
                if (!placed(*p)) continue;
                v.kind = Kind::Waiting;
                v.poll = p;
                return true;
            }
            return false;
        };
        Verdict verdict;
        std::vector<const Poll*> story;
        for (const auto& p : m_polls)
        {
            if (p.trigger != trigger || !(str::EqualsNoCase(p.who, L"Player") || str::EqualsNoCase(p.who, character))) continue;
            if (!verdict.poll) verdict.poll = &p;
            // Sending a companion on moves the scene on only while a scene waits for that companion.
            if (p.story && (!p.sentOnly || CompanionAwaited(now, p, character))) story.push_back(&p);
        }
        if (!verdict.poll)
        {
            ledWaiting(verdict);
            return verdict;
        }
        verdict.kind = Kind::NoStory;
        if (story.empty())
        {
            ledWaiting(verdict);
            return verdict;
        }
        verdict.kind = Kind::NotNow;
        verdict.poll = story.front();
        // The last question about her answered yes, and the scene has asked no more since: the wait
        // is met. A state that asks on while she stands in it (a compound condition, a wait for her to
        // leave) is still waiting.
        if (const auto asked = m_asked.find(trigger); asked != m_asked.end() && asked->second.yes > -1000.0 &&
                                                     asked->second.yes >= asked->second.player && now - asked->second.player > kAskedWithin)
        {
            verdict.kind = Kind::Met;
            return verdict;
        }
        if (!AskedNow(now, trigger, true))
        {
            ledWaiting(verdict);
            return verdict;
        }
        for (const Poll* p : story)
        {
            // The game's own record places a machine it holds: in another state, it does not wait here.
            if (!placed(*p)) continue;
            verdict.kind = Kind::Waiting;
            verdict.poll = p;
            return verdict;
        }
        ledWaiting(verdict);
        return verdict;
    }

    bool HeldWaits::Waiting(const std::wstring& trigger, Waits::Kind kind, bool inside)
    {
        using Kind = Waits::Kind;
        if (kind == Kind::Waiting)
        {
            // In it, the scene is about to move on: it is held no longer.
            if (inside)
                m_held.erase(trigger);
            else
                m_held.insert(trigger);
            return true;
        }
        if (kind == Kind::Met)
        {
            m_held.erase(trigger);
            return false;
        }
        if (kind == Kind::NotNow && m_held.count(trigger))
        {
            if (!inside) return true;
            m_held.erase(trigger);
            return false;
        }
        m_held.erase(trigger);
        return false;
    }

    std::map<std::wstring, double> ParseVariables(const std::wstring& script)
    {
        std::map<std::wstring, double> variables;
        static const std::wregex assignment(LR"re(gVariables\.([A-Za-z0-9_]+)\s*=\s*(-?[0-9]+(?:\.[0-9]+)?))re");
        for (auto it = std::wsregex_iterator(script.begin(), script.end(), assignment); it != std::wsregex_iterator(); ++it)
            variables[(*it)[1].str()] = std::stod((*it)[2].str());
        return variables;
    }

    std::vector<Switch> ParseSwitches(const std::wstring& script, const std::map<std::wstring, double>& variables)
    {
        std::vector<Switch> switches;
        if (script.find(L"SF_SetPlayerCharacter") == std::wstring::npos) return switches;
        static const std::wregex set(LR"re(SF_SetPlayerCharacter\s*\(\s*sm\s*,\s*\{\s*character\s*=\s*"([^"]+)")re");
        static const std::wregex seconds(LR"re(seconds\s*=\s*(gVariables\.([A-Za-z0-9_]+)|-?[0-9]+(?:\.[0-9]+)?))re");
        for (const auto& [name, machine] : ParseMachines(script))
        {
            for (const auto& [number, state] : machine.states)
            {
                std::wsmatch m;
                if (!std::regex_search(state.direct, m, set)) continue;
                Switch sw;
                sw.machine = name;
                sw.state = number;
                sw.character = m[1].str();
                // The state it goes on to, past states that only act and pass on (Ashley's shader
                // set-up at act2_1's telescope): a wait for time whose way on moves the story on.
                auto waiting = machine.states.end();
                auto next = machine.next.find(number);
                for (int steps = 0; steps < 8 && next != machine.next.end(); ++steps)
                {
                    const auto found = machine.states.find(next->second);
                    if (found == machine.states.end()) break;
                    if (WaitsForTime(found->second))
                    {
                        waiting = found;
                        break;
                    }
                    if (!found->second.blocks.empty()) break;
                    next = machine.next.find(next->second);
                }
                if (waiting != machine.states.end())
                {
                    const auto& wait = waiting->second.blocks.front();
                    std::wsmatch w;
                    if (std::regex_search(wait.condition, w, seconds) && MovesOn(CallsIn(wait.text), ActivitiesIn(wait.text), false))
                    {
                        if (w[2].matched)
                        {
                            const auto value = variables.find(w[2].str());
                            if (value != variables.end()) sw.seconds = value->second;
                        }
                        else
                        {
                            sw.seconds = std::stod(w[1].str());
                        }
                    }
                }
                switches.push_back(sw);
            }
        }
        return switches;
    }
}
