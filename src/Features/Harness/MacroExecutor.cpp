#include "MacroExecutor.hpp"

#include <algorithm>
#include <memory>
#include <string>

#include "Entity.hpp"
#include "Features/EntityList.hpp"
#include "Features/Tas/TasController.hpp"
#include "Features/Tas/TasPlayer.hpp"
#include "Features/Tas/TasScript.hpp"
#include "Harness.hpp"
#include "HarnessThread.hpp"
#include "MarkTable.hpp"
#include "Modules/Engine.hpp"
#include "Modules/Server.hpp"
#include "Scheduler.hpp"
#include "Utils.hpp"
#include "Utils/Math.hpp"
#include "Utils/SDK/EntityEdict.hpp"

namespace {

// wait() upper bound (~10s @ 60fps): a verb must not freeze the world forever.
constexpr int kMaxWaitTicks = 600;
// Source view-pitch limit. The TAS re-apply clamps to the cl_pitch* cvars
// regardless; this just keeps look()'s requested angle sane before SetAngles.
constexpr float kPitchLimit = 89.0f;

int Slot() { return GET_SLOT(); }

// Eye position of the host player (slot 0 -> entity index 1), matching the
// engine's own eye calc (Cheats.cpp:315 / AutoAimTool.cpp:86). False if no
// player. Main thread only.
bool PlayerEye(Vector* outEye) {
  ServerEnt* pl = server->GetPlayer(1);
  if (!pl) return false;
  *outEye = pl->abs_origin() + server->GetViewOffset(pl) +
            server->GetPortalLocal(pl).m_vEyeOffset;
  return true;
}

// Mark -> world-space aim point. Revalidates the serial so a recycled slot
// cannot resolve to a different entity, then returns the OBB centre. Sets
// *code = "BAD_MARK" on an absent/stale mark. Main thread only (live entities).
//
// v0 uses the *unrotated* OBB centre (origin + (mins+maxs)/2): correct for the
// axis-aligned PeTI elements of first light. Rotating the local centre by
// abs_angles is the refinement for tilted entities (none in first light).
bool ResolveMarkCenter(int mark, Vector* outCenter, std::string* code) {
  auto [index, serial] = markTable.GetEntityFromMark(mark);
  if (index < 0) {
    *code = "BAD_MARK";
    return false;
  }
  CEntInfo* info = entityList->GetEntityInfoByIndex(index);
  if (!info || !info->m_pEntity ||
      static_cast<uint16_t>(info->m_SerialNumber) != serial) {
    *code = "BAD_MARK";
    return false;
  }
  ServerEnt* se = SE(info->m_pEntity);
  ICollideable& coll = se->collision();  // a reference; the live-entity checks
                                         // above already guarantee validity
  *outCenter = se->abs_origin() + (coll.OBBMins() + coll.OBBMaxs()) * 0.5f;
  return true;
}

QAngle AimAnglesTo(const Vector& eye, const Vector& target) {
  Vector forward = target - eye;
  Vector up{0, 0, 1};
  QAngle a{0, 0, 0};
  Math::VectorAngles(forward, up, &a);
  a.z = 0;  // never roll the view
  return a;
}

// Zero the harness framebulk so no stale movement/keys from a prior verb carry
// in. FetchInputs always reads framebulk[0] -- the same slot Act() writes.
void ClearFramebulk() {
  TasFramebulk& fb = tasPlayer->playbackInfo.slots[0].framebulks[0];
  fb.moveAnalog = {0, 0, 0};
  fb.viewAnalog = {0, 0, 0};
  for (int i = 0; i < TAS_CONTROLLER_INPUT_COUNT; i++)
    fb.buttonStates[i] = false;
}

// THE aiming primitive (plan fact 0.2). Clamps pitch (so the *commanded* angle
// equals what the engine's own clamp will hold) and zeroes roll, sets the
// absolute view, and zeroes the framebulk view delta -- so the TAS per-tick
// re-apply, which computes `GetAngles() - viewAnalog` (TasController.cpp:200 /
// TasPlayer.cpp:663), preserves it (delta 0 => idempotent). Returns the
// commanded angle so the verb can report it (camera == commanded after the tick
// is exactly "SetAngles survived"). If the live spike shows the re-apply
// clobbers it, THIS is the single function to swap (drive a computed viewAnalog
// delta toward `angles` instead). Main thread only.
QAngle ApplyAbsoluteView(QAngle angles) {
  angles.x = std::min(std::max(angles.x, -kPitchLimit), kPitchLimit);
  angles.z = 0;
  ClearFramebulk();  // also zeroes viewAnalog
  engine->SetAngles(Slot(), angles);
  return angles;
}

}  // namespace

portal2_harness::MacroResult MacroExecutor::Execute(
    const portal2_harness::MacroRequest& req) {
  portal2_harness::MacroResult r;

  // Mirror Act()'s preconditions: without harness control + an active TAS
  // player, framebulk writes and tick stepping won't take.
  if (!harness || !harness->harnessControlActive || !tasPlayer ||
      !tasPlayer->IsActive()) {
    r.set_ok(false);
    r.set_result_code("NOT_READY");
    r.set_detail("harness control not active (warmup may be incomplete)");
    return r;
  }

  const std::string& verb = req.verb();
  if (verb == "aim_at") return AimAt(req.mark());
  if (verb == "look") return Look(req.yaw(), req.pitch());
  if (verb == "wait") return Wait(req.ticks());
  if (verb == "done") return Done();

  // go_to / move (PR3), pick_up_cube / release_cube / press / interact (PR4).
  r.set_ok(false);
  r.set_result_code("NOT_IMPLEMENTED");
  r.set_detail("verb '" + verb + "' not implemented yet");
  return r;
}

portal2_harness::MacroResult MacroExecutor::AimAt(int mark) {
  portal2_harness::MacroResult r;

  // Resolve + aim in one main-thread hop. Heap output so a late (post-cancel)
  // closure run never writes a dead stack slot. `target` is the commanded view.
  struct AimOut {
    bool ok = false;
    std::string code = "BAD_MARK";
    QAngle target{0, 0, 0};
  };
  auto out = std::make_shared<AimOut>();
  bool ran = RunOnMainThreadSync(context_, [out, mark]() {
    Vector eye;
    if (!PlayerEye(&eye)) {
      out->code = "NO_PLAYER";
      return;
    }
    Vector center;
    if (!ResolveMarkCenter(mark, &center, &out->code)) return;
    out->target = ApplyAbsoluteView(AimAnglesTo(eye, center));
    out->ok = true;
  });
  if (!ran) {
    r.set_ok(false);
    r.set_result_code("CANCELLED");
    return r;
  }
  if (!out->ok) {
    r.set_ok(false);
    r.set_result_code(out->code);
    r.set_detail("aim_at could not resolve mark " + std::to_string(mark));
    return r;
  }

  // The tick where the TAS re-apply must preserve our aim. The resulting view
  // rides back on the AgentLoop Observe (GameState.camera); we report the
  // *commanded* angle so a caller can verify camera == aim (i.e. SetAngles
  // survived) without a second, cancel-prone main-thread read-back.
  AdvanceTicksBlocking(1);

  r.set_ok(true);
  r.set_result_code("SUCCESS");
  r.set_aim_pitch(out->target.x);
  r.set_aim_yaw(out->target.y);
  r.set_detail(
      Utils::ssprintf("aim pitch=%.1f yaw=%.1f", out->target.x, out->target.y));
  return r;
}

portal2_harness::MacroResult MacroExecutor::Look(int yaw, int pitch) {
  portal2_harness::MacroResult r;

  struct LookOut {
    bool clamped = false;
    QAngle target{0, 0, 0};
  };
  auto out = std::make_shared<LookOut>();
  bool ran = RunOnMainThreadSync(context_, [out, yaw, pitch]() {
    QAngle cur = engine->GetAngles(Slot());
    float reqPitch = cur.x + static_cast<float>(pitch);
    out->target =
        ApplyAbsoluteView(QAngle{reqPitch, cur.y + static_cast<float>(yaw), 0});
    out->clamped = (out->target.x != reqPitch);  // primitive clamped the pitch
  });
  if (!ran) {
    r.set_ok(false);
    r.set_result_code("CANCELLED");
    return r;
  }

  AdvanceTicksBlocking(1);

  r.set_ok(true);
  r.set_result_code("SUCCESS");
  r.set_aim_pitch(out->target.x);
  r.set_aim_yaw(out->target.y);
  std::string detail =
      Utils::ssprintf("aim pitch=%.1f yaw=%.1f", out->target.x, out->target.y);
  if (out->clamped) detail += " (pitch clamped)";
  r.set_detail(detail);
  return r;
}

portal2_harness::MacroResult MacroExecutor::Wait(int ticks) {
  if (ticks < 1) ticks = 1;
  if (ticks > kMaxWaitTicks) ticks = kMaxWaitTicks;

  // Stop any movement/keys a prior verb left in the framebulk, then idle. FIFO
  // guarantees this runs before the AdvanceTick burst (same as Act()).
  Scheduler::OnMainThread([]() { ClearFramebulk(); });
  AdvanceTicksBlocking(ticks);

  portal2_harness::MacroResult r;
  r.set_ok(true);
  r.set_result_code("SUCCESS");
  r.set_detail(Utils::ssprintf("waited %d ticks", ticks));
  return r;
}

portal2_harness::MacroResult MacroExecutor::Done() {
  portal2_harness::MacroResult r;
  r.set_ok(true);
  r.set_result_code("DONE");
  r.set_detail("agent signalled done; success decided client-side");
  return r;
}
