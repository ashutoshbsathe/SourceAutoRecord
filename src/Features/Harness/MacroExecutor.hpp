#pragma once
#include <grpcpp/grpcpp.h>

#include <string>

#include "harness.pb.h"

// Runs one closed semantic verb (aim_at / look / wait / done / ...) against the
// live, frozen game. Like Act(), it lives on the gRPC thread and dispatches
// every engine read/write to the main thread (Scheduler::OnMainThread, via the
// HarnessThread helpers). One instance per macro step; it holds no state beyond
// the gRPC context it needs to abort cleanly if the client drops the stream.
//
// PR2 ships aim_at/look/wait/done; PR3 adds go_to/move; the interaction verbs
// (PR4) still return NOT_IMPLEMENTED.
class MacroExecutor {
 public:
  explicit MacroExecutor(grpc::ServerContext* context) : context_(context) {}

  portal2_harness::MacroResult Execute(
      const portal2_harness::MacroRequest& req);

 private:
  grpc::ServerContext* context_;

  portal2_harness::MacroResult AimAt(int mark);
  portal2_harness::MacroResult Look(int yaw, int pitch);
  portal2_harness::MacroResult GoTo(int mark);
  portal2_harness::MacroResult Move(const std::string& dir, int ticks);
  portal2_harness::MacroResult Wait(int ticks);
  portal2_harness::MacroResult Done();
};
