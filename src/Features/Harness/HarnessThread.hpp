#pragma once
#include <grpcpp/grpcpp.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

#include "Harness.hpp"
#include "Modules/Engine.hpp"
#include "Scheduler.hpp"

// The gRPC handlers run off the main thread, but engine reads/writes must run
// on it (Scheduler::OnMainThread). These wrap the two coordination patterns the
// handlers share.

// Advance n (>= 1) ticks on the main thread, blocking until PRE_TICK counts
// them down (ticksRemaining/tickCV). Set the framebulk first as its own
// OnMainThread closure -- the queue is FIFO, so it runs before this tick burst.
inline void AdvanceTicksBlocking(int n) {
  if (n <= 0) n = 1;
  harness->ticksRemaining = n;
  Scheduler::OnMainThread([n]() {
    for (int i = 0; i < n; i++) {
      engine->AdvanceTick();
    }
  });
  std::unique_lock<std::mutex> lock(harness->tickMutex);
  harness->tickCV.wait(lock, []() { return harness->ticksRemaining <= 0; });
}

// Run fn() on the main thread and block for it, returning false instead of
// spinning forever if the client cancels the stream. On cancel the closure may
// still run later, so it owns copies of fn and the done flag; any outputs fn
// writes by reference must outlive that. context may be null to wait forever.
template <class F>
inline bool RunOnMainThreadSync(grpc::ServerContext* context, F&& fn) {
  auto done = std::make_shared<std::atomic<bool>>(false);
  Scheduler::OnMainThread([done, fn = std::forward<F>(fn)]() mutable {
    fn();
    done->store(true);
  });
  while (!done->load()) {
    if (context && context->IsCancelled()) return false;
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }
  return true;
}
