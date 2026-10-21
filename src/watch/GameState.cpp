#include "watch/GameState.hpp"

#include "core/GameThread.hpp"
#include "core/Log.hpp"
#include "core/ObjectUtil.hpp"

namespace da::gamestate
{
    using RC::Unreal::FProperty;
    using RC::Unreal::UObject;

    namespace
    {
        UObject* g_actor = nullptr;
        State g_state = State::Unknown;

        void Poll(float)
        {
            if (gamethread::FrameCount() % 5 != 0) return;
            if (!g_actor || !obj::IsLive(g_actor)) g_actor = obj::FindFirstLive(L"BatesGameState");
            if (!g_actor) return;
            int64_t value = -1;
            obj::CallReturn(g_actor, L"GetBatesGameState",
                            [&](void* params, FProperty* returnValue)
                            {
                                if (returnValue) obj::ReadIntAt(params, returnValue, value);
                            });
            if (value < 0) return;
            const State state = value <= 9 ? static_cast<State>(value) : State::Unknown;
            if (state == g_state) return;
            log::Info(L"game: state {} -> {}", Name(g_state), Name(state));
            g_state = state;
        }
    }

    State Current()
    {
        return g_state;
    }

    std::wstring Name(State state)
    {
        switch (state)
        {
        case State::None: return L"None";
        case State::Booting: return L"Booting";
        case State::Menu: return L"Menu";
        case State::Paused: return L"Paused";
        case State::Loading: return L"Loading";
        case State::Game: return L"Game";
        case State::GameStarting: return L"GameStarting";
        case State::GameEnding: return L"GameEnding";
        case State::LoadingNewLevel: return L"LoadingNewLevel";
        case State::FreezeScreen: return L"FreezeScreen";
        default: return L"unknown";
        }
    }

    bool InPlay()
    {
        return g_state == State::Game || g_state == State::GameStarting || g_state == State::GameEnding || g_state == State::FreezeScreen;
    }

    bool Paused()
    {
        return g_state == State::Paused;
    }

    void Install()
    {
        gamethread::AddPoller(L"gamestate", &Poll);
    }

    void Reset()
    {
        g_actor = nullptr;
    }
}
