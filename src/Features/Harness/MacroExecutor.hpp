#pragma once
#include <grpcpp/grpcpp.h>

#include <string>

#include "harness.pb.h"

// Runs one closed semantic verb (aim_at / look / go_to / move / pick_up / ...)
// against the live, frozen game. Like Act(), it lives on the gRPC thread and
// dispatches every engine read/write to the main thread
// (Scheduler::OnMainThread, via the HarnessThread helpers). One instance per
// macro step; it holds no state beyond the gRPC context it needs to abort
// cleanly if the client drops the stream.
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
  portal2_harness::MacroResult PickUp(int mark);
  portal2_harness::MacroResult Release(int mark);
  portal2_harness::MacroResult Interact(int mark);
  portal2_harness::MacroResult Interpose(
      const portal2_harness::MacroRequest& req);
  // Yaw a seated cube's +X at a target + confirm power, re-seating to re-roll
  // the ~2% settle jank. POWERED / NOT_POWERED / CANCELLED; *residual = degrees
  // the cube's +X ends off the target.
  std::string RedirectConfirm(uint32_t cubeKey, int targetMark,
                              float* residual);
  portal2_harness::MacroResult RedirectTo(
      const portal2_harness::MacroRequest& req);
};
