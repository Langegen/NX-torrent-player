#pragma once

#include <functional>

// The app's network work, a few requests at a time. brls::async runs every
// task on ONE thread, one after another, with a half-second nap between
// batches: a card's details queued behind a hundred home-screen posters, and
// a Jackett search held up everything for its ten seconds. This is a small
// pool beside it.
//
// - kThreads workers, kept under libnx's 16 concurrent blocking socket calls
//   with the engine's own (~5) in use.
// - High priority (what someone is waiting on: details, a search, sources)
//   is always taken before Low (images).
// - Tasks run concurrently and in no set order: anything that must happen in
//   order (the history and library writes, a Stremio progress push) stays on
//   brls::async.
// - As with brls::async, hand results back to the UI with brls::sync.
//
// The threads are never detached: libnx has no pthread_detach (it returns
// ENOSYS, so std::thread::detach() throws -- that closed the app on the first
// request). They are kept and joined by stop(). A thread that cannot be made
// leaves the work to brls::async; a task that throws is logged, not fatal.
namespace workers
{

enum class Priority
{
    High,
    Low,
};

constexpr int kThreads = 4;

void run(std::function<void()> task, Priority p = Priority::High);

// As the app quits, before main() returns: aborts the requests in flight,
// drops what has not started, and joins the threads. A network call still
// running past main() is a crash on the Switch (socketExit runs under it).
void stop();

} // namespace workers
