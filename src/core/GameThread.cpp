#include "core/GameThread.hpp"

#include "core/Fault.hpp"
#include "core/Log.hpp"
#include "core/Strings.hpp"

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

        struct Registered
        {
            std::wstring name;
            std::wstring label; // "poller 'name'", the site of its faults and failures in the log
            Poller poller;
        };
        // A deque: a poller registered while the pollers run (the features register theirs
        // from the startup poller) moves none of the others while one of them is running.
        std::deque<Registered> g_pollers;
        std::vector<float> g_pollerFrameMs; // the pollers' total per frame since the last take
        std::vector<float> g_pollerWorst;   // each poller's longest run since the last take
        std::atomic<std::thread::id> g_gameThreadId{};
        std::atomic<unsigned long long> g_frame{0};
        std::chrono::steady_clock::time_point g_firstTick{};
        bool g_installed = false;

        struct TaskRun
        {
            Task* task;
        };
        void RunTask(void* context)
        {
            (*static_cast<TaskRun*>(context)->task)();
        }

        struct PollerRun
        {
            const Poller* poller;
            float deltaSeconds;
        };
        void RunPoller(void* context)
        {
            auto* run = static_cast<PollerRun*>(context);
            (*run->poller)(run->deltaSeconds);
        }

        // Runs one piece of the mod's work on the game thread. A memory fault in it is held
        // by the boundary, which also releases what the fault left locked; a C++ exception is
        // logged. Either way the next piece runs, this frame and the next.
        void Guarded(const std::wstring& what, void (*fn)(void*), void* context)
        {
            try
            {
                fault::InvokeLogged(what.c_str(), fn, context);
            }
            catch (const std::exception& e)
            {
                log::Error(L"{} failed: {}", what, str::Utf8ToWide(e.what()));
            }
            catch (...)
            {
                log::Error(L"{} failed (unknown exception)", what);
            }
        }

        void OnEngineTick(RC::Unreal::Hook::TCallbackIterationData<void>&, RC::Unreal::UEngine*, float deltaSeconds, bool)
        {
            g_gameThreadId = std::this_thread::get_id();
            if (g_firstTick.time_since_epoch().count() == 0) g_firstTick = std::chrono::steady_clock::now();
            ++g_frame;

            std::deque<Task> tasks;
            {
                fault::Guard lock(g_queueMutex, L"gamethread.queue");
                tasks.swap(g_queue);
            }
            for (auto& task : tasks)
            {
                TaskRun run{&task};
                Guarded(L"game thread task", &RunTask, &run);
            }

            // Each poller is timed, so that the mod's share of a slow frame is known. The count
            // is read again on every round: a poller registered by the one running runs in the
            // same frame, behind it, and gets its own slot in the record.
            float frameMs = 0.0f;
            for (size_t i = 0; i < g_pollers.size(); ++i)
            {
                const Registered& entry = g_pollers[i];
                const auto started = std::chrono::steady_clock::now();
                PollerRun run{&entry.poller, deltaSeconds};
                Guarded(entry.label, &RunPoller, &run);
                const float ms = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started).count();
                frameMs += ms;
                if (g_pollerWorst.size() <= i) g_pollerWorst.resize(g_pollers.size(), 0.0f);
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
        fault::Guard lock(g_queueMutex, L"gamethread.queue");
        g_queue.push_back(std::move(task));
    }

    void AddPoller(std::wstring name, Poller poller)
    {
        std::wstring label = L"poller '" + name + L"'";
        g_pollers.push_back(Registered{std::move(name), std::move(label), std::move(poller)});
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
            costs.worst.emplace_back(g_pollers[i].name, g_pollerWorst[i]);
        std::fill(g_pollerWorst.begin(), g_pollerWorst.end(), 0.0f);
        return costs;
    }
}
