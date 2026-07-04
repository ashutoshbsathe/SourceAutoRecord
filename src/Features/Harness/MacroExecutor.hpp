#pragma once
#include <grpcpp/grpcpp.h>

#include <string>

#include "harness.pb.h"

// Runs one semantic verb (aim_at / look / go_to / move / pick_up / ...) against
// the frozen game. Lives on the gRPC thread and dispatches every engine
// read/write to the main thread via the HarnessThread helpers. One instance per
// macro step; holds only the gRPC context, used to abort if the client drops
// the stream.
class MacroExecutor {
 public:
  explicit MacroExecutor(grpc::ServerContext* context) : context_(context) {}

  portal2_harness::MacroResult Execute(
      const portal2_harness::MacroRequest& req);

 private:
  grpc::ServerContext* context_;

  portal2_harness::MacroResult AimAt(const std::string& target);
  portal2_harness::MacroResult Look(int yaw, int pitch);
  portal2_harness::MacroResult GoTo(const std::string& target);
  portal2_harness::MacroResult Move(const std::string& dir, int ticks);
  portal2_harness::MacroResult Wait(int ticks);
  portal2_harness::MacroResult Done();
  portal2_harness::MacroResult PickUp(const std::string& target);
  portal2_harness::MacroResult Release(const std::string& target);
  portal2_harness::MacroResult Interact(const std::string& target);
  portal2_harness::MacroResult Interpose(
      const portal2_harness::MacroRequest& req);
  // Yaw a seated cube's +X at a target and confirm power, re-seating to retry
  // the occasional settle jank. Returns POWERED / NOT_POWERED / CANCELLED;
  // *residual = degrees the cube's +X ends off the target.
  std::string RedirectConfirm(uint32_t cubeKey, int targetMark,
                              float* residual);
  portal2_harness::MacroResult RedirectTo(
      const portal2_harness::MacroRequest& req);
  portal2_harness::MacroResult PlacePortal(
      const portal2_harness::MacroRequest& req);
  portal2_harness::MacroResult PassThrough(
      const portal2_harness::MacroRequest& req);
  portal2_harness::MacroResult JumpInto(const std::string& target);
  portal2_harness::MacroResult DropInto(
      const portal2_harness::MacroRequest& req);
};
