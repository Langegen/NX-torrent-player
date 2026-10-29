#include "workers.hpp"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace workers
{

namespace
{

std::mutex lock;
std::condition_variable wake;
std::deque<std::function<void()>> high, low;
bool started = false;

void loop()
{
    for (;;)
    {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> g(lock);
            wake.wait(g, [] { return !high.empty() || !low.empty(); });
            std::deque<std::function<void()>>& q = high.empty() ? low : high;
            task = std::move(q.front());
            q.pop_front();
        }
        task();
    }
}

} // namespace

void run(std::function<void()> task, Priority p)
{
    {
        std::lock_guard<std::mutex> g(lock);
        if (!started)
        {
            started = true;
            // Detached: they sleep on the condition when idle, and the process
            // exit takes them down (nothing they hold needs a clean close).
            for (int i = 0; i < kThreads; i++) std::thread(loop).detach();
        }
        (p == Priority::High ? high : low).push_back(std::move(task));
    }
    wake.notify_one();
}

} // namespace workers
