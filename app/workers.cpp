#include "workers.hpp"

#include <borealis.hpp>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <exception>
#include <mutex>
#include <thread>
#include <vector>

#include "http.hpp"
#ifdef __SWITCH__
extern "C" {
#include "thread_cfg.h"
}
#endif

namespace workers
{

namespace
{

std::mutex lock;
std::condition_variable wake;
std::deque<std::function<void()>> high, low;
std::vector<std::thread*> threads;  // kept to be joined: never detached
bool started  = false;
bool stopping = false;
std::atomic<uint64_t> s_lowGen{ 1 };

void runGuarded(const std::function<void()>& task)
{
    // An escaped exception would be std::terminate: the app closing.
    try
    {
        task();
    }
    catch (const std::exception& e)
    {
        brls::Logger::error("[workers] a task threw: {}", e.what());
    }
    catch (...)
    {
        brls::Logger::error("[workers] a task threw");
    }
}

void loop()
{
#ifdef __SWITCH__
    tsnx_thread_pin_self(TSNX_CORE_WORK, TSNX_PRIO_IMAGE);
#endif
    for (;;)
    {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> g(lock);
            wake.wait(g, [] { return stopping || !high.empty() || !low.empty(); });
            if (stopping) return;
            std::deque<std::function<void()>>& q = high.empty() ? low : high;
            task = std::move(q.front());
            q.pop_front();
        }
        runGuarded(task);
    }
}

// Under `lock`. Threads are made on first use; a failure (the system out of
// threads) is logged and the pool runs with what it got -- or with none.
void startLocked()
{
    started = true;
    for (int i = 0; i < kThreads; i++)
    {
        try
        {
            threads.push_back(new std::thread(loop));
        }
        catch (const std::exception& e)
        {
            brls::Logger::error("[workers] thread {} not started: {}", i, e.what());
            break;
        }
    }
}

} // namespace

void run(std::function<void()> task, Priority p)
{
    bool pooled;
    {
        std::lock_guard<std::mutex> g(lock);
        if (stopping) return;
        if (!started) startLocked();
        pooled = !threads.empty();
        if (pooled) (p == Priority::High ? high : low).push_back(std::move(task));
    }
    if (pooled)
        wake.notify_one();
    else
        // No thread of our own: borealis' single task thread still works.
        brls::async([task]() { runGuarded(task); });
}

uint64_t lowGeneration()
{
    return s_lowGen.load(std::memory_order_acquire);
}

void bumpLowGeneration()
{
    s_lowGen.fetch_add(1, std::memory_order_acq_rel);
}

void stop()
{
    {
        std::lock_guard<std::mutex> g(lock);
        if (stopping) return;
        stopping = true;
        high.clear();
        low.clear();
    }
    http::abortAll();  // the busy ones return within about a second
    wake.notify_all();
    for (std::thread* t : threads)
    {
        if (t->joinable()) t->join();
        delete t;
    }
    threads.clear();
}

} // namespace workers
