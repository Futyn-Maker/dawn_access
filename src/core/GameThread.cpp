#include "core/GameThread.hpp"

#include "core/Log.hpp"

#include <Unreal/Hooks.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace da::gamethread
{
    namespace
    {
        std::mutex g_queueMutex;
        std::deque<Task> g_queue;
        std::vector<std::pair<std::wstring, Poller>> g_pollers;
        std::vector<float> g_pollerFrameMs; // the pollers' total per frame since the last take
        std::vector<float> g_pollerWorst;   // each poller's longest run since the last take
        std::atomic<std::thread::id> g_gameThreadId{};
        std::atomic<unsigned long long> g_frame{0};
        std::chrono::steady_clock::time_point g_firstTick{};
        bool g_installed = false;

        void OnEngineTick(RC::Unreal::Hook::TCallbackIterationData<void>&, RC::Unreal::UEngine*, float deltaSeconds, bool)
        {
            g_gameThreadId = std::this_thread::get_id();
            if (g_firstTick.time_since_epoch().count() == 0) g_firstTick = std::chrono::steady_clock::now();
            ++g_frame;

            std::deque<Task> tasks;
            {
                std::lock_guard lock(g_queueMutex);
                tasks.swap(g_queue);
            }
            for (auto& task : tasks)
            {
                try
                {
                    task();
                }
                catch (const std::exception& e)
                {
                    log::Error(L"game thread task failed: {}", std::wstring(e.what(), e.what() + strlen(e.what())));
                }
                catch (...)
                {
                    log::Error(L"game thread task failed (unknown exception)");
                }
            }

            // Each poller is timed, so that the mod's share of a slow frame is known.
            if (g_pollerWorst.size() != g_pollers.size()) g_pollerWorst.assign(g_pollers.size(), 0.0f);
            float frameMs = 0.0f;
            for (size_t i = 0; i < g_pollers.size(); ++i)
            {
                auto& [name, poller] = g_pollers[i];
                const auto started = std::chrono::steady_clock::now();
                try
                {
                    poller(deltaSeconds);
                }
                catch (const std::exception& e)
                {
                    log::Error(L"poller '{}' failed: {}", name, std::wstring(e.what(), e.what() + strlen(e.what())));
                }
                catch (...)
                {
                    log::Error(L"poller '{}' failed (unknown exception)", name);
                }
                const float ms = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started).count();
                frameMs += ms;
                g_pollerWorst[i] = std::max(g_pollerWorst[i], ms);
            }
            g_pollerFrameMs.push_back(frameMs);
            if (g_pollerFrameMs.size() > 20000) g_pollerFrameMs.erase(g_pollerFrameMs.begin(), g_pollerFrameMs.begin() + 10000);
        }
    }

    void Install()
    {
        if (g_installed) return;
        g_installed = true;
        RC::Unreal::Hook::FCallbackOptions options{};
        options.bReadonly = true;
        options.OwnerModName = STR("DawnAccess");
        options.HookName = STR("GameThreadPump");
        RC::Unreal::Hook::RegisterEngineTickPostCallback(&OnEngineTick, options);
        log::Info(L"game thread pump installed (engine tick post callback)");
    }

    void Post(Task task)
    {
        std::lock_guard lock(g_queueMutex);
        g_queue.push_back(std::move(task));
    }

    void AddPoller(std::wstring name, Poller poller)
    {
        g_pollers.emplace_back(std::move(name), std::move(poller));
    }

    bool IsGameThread()
    {
        return std::this_thread::get_id() == g_gameThreadId.load();
    }

    unsigned long long FrameCount()
    {
        return g_frame.load();
    }

    double NowSeconds()
    {
        if (g_firstTick.time_since_epoch().count() == 0) return 0.0;
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - g_firstTick).count();
    }

    PollerCosts TakePollerCosts()
    {
        PollerCosts costs;
        costs.frameMs.swap(g_pollerFrameMs);
        for (size_t i = 0; i < g_pollers.size() && i < g_pollerWorst.size(); ++i)
            costs.worst.emplace_back(g_pollers[i].first, g_pollerWorst[i]);
        std::fill(g_pollerWorst.begin(), g_pollerWorst.end(), 0.0f);
        return costs;
    }
}
