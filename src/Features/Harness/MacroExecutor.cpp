#include "MacroExecutor.hpp"

#include <algorithm>
#include <cmath>
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
#include "Utils/SDK/Trace.hpp"

namespace {

// wait() upper bound (~10s @ 60fps): a verb must not freeze the world forever.
constexpr int kMaxWaitTicks = 600;
// Source view-pitch limit. The TAS re-apply clamps to the cl_pitch* cvars
// regardless; this just keeps look()'s requested angle sane before SetAngles.
constexpr float kPitchLimit = 89.0f;

// go_to / move march tuning (v0 consts; flat chambers).
constexpr int kGoToTickBatch = 4;   // ticks advanced per march iteration
constexpr int kGoToMaxTicks = 400;  // give up after this many ticks
constexpr int kGoToSettle = 24;  // ticks to bleed off walk velocity at the end
constexpr float kReachRadius = 48.0f;  // horizontal dist that counts as arrived
constexpr float kStuckEps = 1.0f;      // <this much progress/iter twice = stuck
constexpr float kProbeHeight = 18.0f;  // lift guard rays to ~step height
constexpr float kWallProbe = 24.0f;    // forward wall-ray length
constexpr float kStepAhead = 24.0f;    // edge ray is cast this far ahead
constexpr float kStepDownMax = 64.0f;  // no floor within this below = edge

// Interaction-verb tuning (pick_up / release / interact).
constexpr int kSettle = 20;       // ticks to let a grab/drop/use resolve
constexpr int kUseHoldTicks = 3;  // hold +use past the press/tick-advance race
constexpr float kGrabRange = 96.0f;   // reach gate: ~80u radius + half a cube
constexpr float kHeldDist = 80.0f;    // a held object rides within this of eye
constexpr float kMinGrabMove = 8.0f;  // a real grab snaps it more than this
constexpr float kReleasePitch = 75.0f;  // mark-less release: look-down pitch

int Slot() { return GET_SLOT(); }

// Wrap a yaw to [-180, 180]. Keeps reported aim angles readable and stops the
// cumulative look() (cur.y + yaw) from drifting unbounded across many turns, so
// the model never has to do +-360 mod-arithmetic on its own facing.
float NormalizeYaw(float yaw) {
  yaw = std::fmod(yaw, 360.0f);
  if (yaw > 180.0f) yaw -= 360.0f;
  if (yaw < -180.0f) yaw += 360.0f;
  return yaw;
}

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

// Mark -> live CEntInfo. Revalidates the serial so a recycled slot cannot
// resolve to a different entity. Sets *code = "BAD_MARK" on an absent/stale
// mark. Main thread only (touches the live entity list).
bool ResolveMarkInfo(int mark, CEntInfo** outInfo, std::string* code) {
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
  *outInfo = info;
  return true;
}

// Classes pick_up can actually carry via +use. Gating on this keeps a wrong
// mark (e.g. a button) from being +use-pressed as a side effect of a bad grab.
bool IsGrabbableClass(const char* cls) {
  if (!cls) return false;
  std::string s(cls);
  return s == "prop_weighted_cube" || s == "prop_monster_box" ||
         s == "npc_portal_turret_floor";
}

// OBB centre of an entity. Uses the *unrotated* centre (origin +
// (mins+maxs)/2): correct for axis-aligned PeTI elements; rotating the local
// centre by abs_angles is the refinement for tilted entities. Main thread only
// (live entity).
Vector EntityCenter(ServerEnt* se) {
  ICollideable& coll = se->collision();
  return se->abs_origin() + (coll.OBBMins() + coll.OBBMaxs()) * 0.5f;
}

// Mark -> world-space aim point (OBB centre). Sets *code on a bad mark. Main
// thread only.
bool ResolveMarkCenter(int mark, Vector* outCenter, std::string* code) {
  CEntInfo* info = nullptr;
  if (!ResolveMarkInfo(mark, &info, code)) return false;
  *outCenter = EntityCenter(SE(info->m_pEntity));
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

// THE aiming primitive. Clamps pitch (so the *commanded* angle equals what the
// engine's own clamp will hold) and zeroes roll, sets the absolute view, and
// zeroes the framebulk view delta -- so the TAS per-tick re-apply, which
// computes `GetAngles() - viewAnalog` (TasController.cpp:200 /
// TasPlayer.cpp:663), preserves it (delta 0 => idempotent). Returns the
// commanded angle so the verb can report it (camera == commanded after the tick
// is exactly "SetAngles survived"). If the re-apply ever clobbers it, THIS is
// the single function to swap (drive a computed viewAnalog delta toward
// `angles` instead). Main thread only.
QAngle ApplyAbsoluteView(QAngle angles) {
  angles.x = std::min(std::max(angles.x, -kPitchLimit), kPitchLimit);
  angles.y =
      NormalizeYaw(angles.y);  // commanded == reported, bounded [-180,180]
  angles.z = 0;
  ClearFramebulk();  // also zeroes viewAnalog
  engine->SetAngles(Slot(), angles);
  return angles;
}

// Body-frame analog move for a relative direction, plus the world-yaw offset
// that direction adds to the player's facing (so the guard probes where the
// player will actually translate). moveAnalog is {x=sidemove(right+),
// y=forwardmove(forward+)}; +side strafes right == facing - 90 deg.
struct MoveDir {
  float side, fwd, yawOffset;
};
bool ParseMoveDir(const std::string& dir, MoveDir* out) {
  if (dir == "forward") return *out = {0, 1, 0}, true;
  if (dir == "back") return *out = {0, -1, 180}, true;
  if (dir == "left") return *out = {-1, 0, 90}, true;
  if (dir == "right") return *out = {1, 0, -90}, true;
  return false;
}

// Set the framebulk to march in body frame, holding the current view (zero view
// delta, no SetAngles). Main thread.
void SetMoveFramebulk(float side, float fwd) {
  TasFramebulk& fb = tasPlayer->playbackInfo.slots[0].framebulks[0];
  fb.moveAnalog = {side, fwd, 0};
  fb.viewAnalog = {0, 0, 0};
  for (int i = 0; i < TAS_CONTROLLER_INPUT_COUNT; i++)
    fb.buttonStates[i] = false;
}

// Press +use, release, then let the world settle. Source's +use pickup is
// edge-triggered: m_afButtonPressed&IN_USE is set only on the tick IN_USE goes
// 0->1, and PlayerUse grabs/drops/activates on that single edge. We hold the
// button for kUseHoldTicks rather than one tick because a 1-tick framebulk
// press can be cleared (the release below) before the simulated tick that runs
// PlayerUse ever reads it -- a race between this verb thread's release and the
// tick advance (worse if sv_alternateticks is on, where one
// AdvanceTicksBlocking drives a 2-sim-tick pair). Holding a few ticks keeps
// IN_USE asserted across enough simulated ticks that PlayerUse sees the edge.
// It stays ONE logical press: the edge is only on the 0->1 transition, so extra
// held ticks add no new edge and can't double-toggle (grab-then-drop) for ANY
// verb. Fire-and-forget framebulk writes, FIFO-ordered ahead of the AdvanceTick
// burst (same discipline as Wait()); `settle` ticks then let the physics/grab
// resolve.
//
// gRPC (verb) thread ONLY: it blocks on the tick condvar via
// AdvanceTicksBlocking (the tick countdown only runs on the main thread), so
// calling it from inside a main-thread closure would self-deadlock. The "Main
// thread only" helpers above are the opposite contract.
void PulseUse(int settle) {
  Scheduler::OnMainThread([]() {
    ClearFramebulk();
    tasPlayer->playbackInfo.slots[0].framebulks[0].buttonStates[Use] = true;
  });
  AdvanceTicksBlocking(kUseHoldTicks);
  Scheduler::OnMainThread([]() { ClearFramebulk(); });  // release +use
  AdvanceTicksBlocking(settle);
}

// Edge/wall guard: probe along worldYaw from the player's feet. Returns "" if
// clear to march, "WALL" if a solid is right ahead, "EDGE" if the floor drops
// away just ahead. Minimal (one forward ray + one down ray) -- enough for the
// flat chambers; a fan-of-rays nav-compass is a follow-on. `player`
// is the trace pass-entity so the rays don't hit the player itself. Main
// thread.
std::string CheckGuard(void* player, const Vector& feet, float worldYaw) {
  CTraceFilterSimple filter;
  filter.SetPassEntity(player);

  // Forward wall ray at ~step height.
  Vector wallStart = feet + Vector{0, 0, kProbeHeight};
  QAngle fwdAng{0, worldYaw, 0};
  CGameTrace wall;
  if (engine->Trace(wallStart, fwdAng, kWallProbe, MASK_PLAYERSOLID, filter,
                    wall)) {
    return "WALL";
  }

  // Floor ray straight down from a point kStepAhead ahead; no hit = no floor.
  Vector fwdDir;
  Math::AngleVectors(fwdAng, &fwdDir);
  Vector aheadStart = feet + fwdDir * kStepAhead + Vector{0, 0, kProbeHeight};
  QAngle downAng{90, 0, 0};
  CGameTrace floorTr;
  if (!engine->Trace(aheadStart, downAng, kProbeHeight + kStepDownMax,
                     MASK_PLAYERSOLID, filter, floorTr)) {
    return "EDGE";
  }
  return "";
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
  if (verb == "go_to") return GoTo(req.mark());
  if (verb == "move") return Move(req.dir(), req.ticks());
  if (verb == "wait") return Wait(req.ticks());
  if (verb == "done") return Done();
  if (verb == "pick_up") return PickUp(req.mark());
  if (verb == "release") return Release(req.mark());
  if (verb == "interact") return Interact(req.mark());

  // press (a pedestal-button alias of interact) is not wired yet.
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

portal2_harness::MacroResult MacroExecutor::GoTo(int mark) {
  portal2_harness::MacroResult r;

  // Resolve the target once (static for these chambers) and capture the
  // starting distance, so an early cancel reports a real distance, not 0.
  struct Resolve {
    bool ok = false;
    std::string code = "BAD_MARK";
    Vector center{0, 0, 0};
    Vector startFeet{0, 0, 0};  // for moved_dist (distance actually walked)
    float initialDist = 0;
  };
  auto res = std::make_shared<Resolve>();
  bool ran = RunOnMainThreadSync(context_, [res, mark]() {
    if (!ResolveMarkCenter(mark, &res->center, &res->code)) return;
    res->ok = true;
    ServerEnt* pl = server->GetPlayer(1);
    if (pl) {
      Vector feet = pl->abs_origin();
      res->startFeet = feet;
      res->initialDist =
          Vector{res->center.x - feet.x, res->center.y - feet.y, 0}.Length2D();
    }
  });
  if (!ran) {
    r.set_ok(false);
    r.set_result_code("CANCELLED");
    return r;
  }
  if (!res->ok) {
    r.set_ok(false);
    r.set_result_code(res->code);
    r.set_detail("go_to could not resolve mark " + std::to_string(mark));
    return r;
  }

  // Closed march loop: each batch re-aim level at the target, probe the guard,
  // step forward; stop on arrival, no-progress (stuck), guard, max-ticks, or a
  // dropped stream. The per-batch RunOnMainThreadSync honors cancellation.
  struct Step {
    bool reached = false;
    std::string guard;  // "", "WALL", "EDGE", "NO_PLAYER"
    float dist = 0;
    Vector pos{0, 0, 0};
  };
  Vector target = res->center;
  Vector lastPos{0, 0, 0};
  bool havePrev = false;
  int stuckRuns = 0;
  float finalDist = res->initialDist;
  bool reached = false;
  std::string code = "UNREACHABLE";

  for (int t = 0; t < kGoToMaxTicks; t += kGoToTickBatch) {
    auto s = std::make_shared<Step>();
    bool ok = RunOnMainThreadSync(context_, [s, target]() {
      ServerEnt* pl = server->GetPlayer(1);
      if (!pl) {
        s->guard = "NO_PLAYER";
        return;
      }
      Vector feet = pl->abs_origin();
      s->pos = feet;
      Vector forward{target.x - feet.x, target.y - feet.y, 0};
      s->dist = forward.Length2D();
      if (s->dist <= kReachRadius) {
        s->reached = true;
        return;
      }
      Vector up{0, 0, 1};
      QAngle a{0, 0, 0};
      Math::VectorAngles(forward, up, &a);
      std::string g = CheckGuard(pl, feet, a.y);
      if (!g.empty()) {
        s->guard = g;
        return;
      }
      ApplyAbsoluteView(QAngle{0, a.y, 0});  // level aim at the target
      SetMoveFramebulk(0, 1);                // walk forward
    });
    if (!ok) {
      r.set_ok(false);
      r.set_result_code("CANCELLED");
      r.set_final_dist(finalDist);
      return r;
    }

    finalDist = s->dist;
    if (s->reached) {
      reached = true;
      code = "SUCCESS";
      break;
    }
    if (!s->guard.empty()) {
      code = (s->guard == "NO_PLAYER") ? "NO_PLAYER" : "BLOCKED";
      r.set_detail("go_to blocked by " + s->guard);
      break;
    }
    if (havePrev) {
      Vector d{s->pos.x - lastPos.x, s->pos.y - lastPos.y, 0};
      stuckRuns = (d.Length2D() < kStuckEps) ? stuckRuns + 1 : 0;
      if (stuckRuns >= 2) {
        code = "STUCK";
        break;
      }
    }
    lastPos = s->pos;
    havePrev = true;

    AdvanceTicksBlocking(kGoToTickBatch);
  }

  // Stop: zero the framebulk and advance kGoToSettle ticks to bleed off most of
  // the walk velocity, so the player doesn't coast onto the target on the next
  // verb (a coast onto a cube wrecks the grab). Friction (not an instant write)
  // does the bleeding, so read the near-rest -- not exactly settled --
  // distance.
  Scheduler::OnMainThread([]() { ClearFramebulk(); });
  AdvanceTicksBlocking(kGoToSettle);
  Vector finalFeet = res->startFeet;
  {
    auto fin = std::make_shared<float>(finalDist);
    auto feet = std::make_shared<Vector>(res->startFeet);
    RunOnMainThreadSync(context_, [fin, feet, target]() {
      ServerEnt* pl = server->GetPlayer(1);
      if (pl) {
        *feet = pl->abs_origin();
        *fin = Vector{target.x - feet->x, target.y - feet->y, 0}.Length2D();
      }
    });
    finalDist = *fin;
    finalFeet = *feet;
  }
  float moved =
      Vector{finalFeet.x - res->startFeet.x, finalFeet.y - res->startFeet.y, 0}
          .Length2D();

  // A blocked/stuck march that still covered real ground is ADVANCED, not a
  // failure: the model should re-plan from its new spot, not retry the same
  // verb. Pinned (moved ~0) keeps the honest BLOCKED/STUCK/UNREACHABLE code.
  bool advanced =
      !reached && moved > kReachRadius &&
      (code == "BLOCKED" || code == "STUCK" || code == "UNREACHABLE");
  std::string why =
      r.detail();  // "go_to blocked by WALL/EDGE" if a guard tripped
  r.set_ok(reached || advanced);
  r.set_result_code(advanced ? "ADVANCED" : code);
  r.set_reached(reached);
  r.set_final_dist(finalDist);
  r.set_moved_dist(moved);
  if (advanced)
    r.set_detail(Utils::ssprintf("advanced %.0f units, %.0f to go (%s)", moved,
                                 finalDist,
                                 why.empty() ? code.c_str() : why.c_str()));
  else if (r.detail().empty())
    r.set_detail(Utils::ssprintf("dist=%.0f after march", finalDist));
  return r;
}

portal2_harness::MacroResult MacroExecutor::Move(const std::string& dir,
                                                 int ticks) {
  portal2_harness::MacroResult r;

  MoveDir md;
  if (!ParseMoveDir(dir, &md)) {
    r.set_ok(false);
    r.set_result_code("BAD_DIR");
    r.set_detail("move: unknown dir '" + dir + "'");
    return r;
  }
  if (ticks < 1) ticks = 1;
  if (ticks > kGoToMaxTicks) ticks = kGoToMaxTicks;

  // Record the start position (for moved_dist).
  auto start = std::make_shared<Vector>(Vector{0, 0, 0});
  auto startOk = std::make_shared<bool>(false);
  bool ran = RunOnMainThreadSync(context_, [start, startOk]() {
    ServerEnt* pl = server->GetPlayer(1);
    if (!pl) return;
    *start = pl->abs_origin();
    *startOk = true;
  });
  if (!ran) {
    r.set_ok(false);
    r.set_result_code("CANCELLED");
    return r;
  }
  if (!*startOk) {
    r.set_ok(false);
    r.set_result_code("NO_PLAYER");
    r.set_detail("move: no player");
    return r;
  }

  // Hold the move key in batches up to `ticks`, edge/wall-guarded each slice.
  struct Step {
    std::string guard;
    Vector pos{0, 0, 0};
  };
  Vector lastPos = *start;
  bool havePrev = false;
  int stuckRuns = 0;
  std::string code = "COMPLETED";
  Vector finalPos = *start;

  for (int done = 0; done < ticks; done += kGoToTickBatch) {
    int batch = std::min(kGoToTickBatch, ticks - done);
    auto s = std::make_shared<Step>();
    bool ok = RunOnMainThreadSync(context_, [s, md]() {
      ServerEnt* pl = server->GetPlayer(1);
      if (!pl) {
        s->guard = "NO_PLAYER";
        return;
      }
      Vector feet = pl->abs_origin();
      s->pos = feet;
      float worldYaw = engine->GetAngles(Slot()).y + md.yawOffset;
      std::string g = CheckGuard(pl, feet, worldYaw);
      if (!g.empty()) {
        s->guard = g;
        return;
      }
      SetMoveFramebulk(md.side, md.fwd);
    });
    if (!ok) {
      r.set_ok(false);
      r.set_result_code("CANCELLED");
      r.set_moved_dist(
          Vector{finalPos.x - start->x, finalPos.y - start->y, 0}.Length2D());
      return r;
    }
    finalPos = s->pos;
    if (!s->guard.empty()) {
      code = s->guard;
      break;
    }
    if (havePrev) {
      Vector d{s->pos.x - lastPos.x, s->pos.y - lastPos.y, 0};
      stuckRuns = (d.Length2D() < kStuckEps) ? stuckRuns + 1 : 0;
      if (stuckRuns >= 2) {
        code = "STUCK";
        break;
      }
    }
    lastPos = s->pos;
    havePrev = true;

    AdvanceTicksBlocking(batch);
  }

  // True final position after the last advance.
  auto fin = std::make_shared<Vector>(finalPos);
  bool finRan = RunOnMainThreadSync(context_, [fin]() {
    ServerEnt* pl = server->GetPlayer(1);
    if (pl) *fin = pl->abs_origin();
  });
  float moved = Vector{fin->x - start->x, fin->y - start->y, 0}.Length2D();
  if (!finRan) {  // stream dropped during the final read -- don't claim success
    r.set_ok(false);
    r.set_result_code("CANCELLED");
    r.set_moved_dist(moved);
    return r;
  }

  // A guard-stopped move that still covered real ground is ADVANCED (progress
  // to re-plan from), not a flat WALL/EDGE/STUCK failure the model reads as
  // "moved nowhere". Pinned (moved ~0) keeps the honest guard code.
  bool advanced = moved > kReachRadius &&
                  (code == "WALL" || code == "EDGE" || code == "STUCK");
  r.set_ok(code == "COMPLETED" || advanced);
  r.set_result_code(advanced ? "ADVANCED" : code);
  r.set_moved_dist(moved);
  r.set_detail(
      advanced ? Utils::ssprintf("moved %.0f units then %s (ADVANCED)", moved,
                                 code.c_str())
               : Utils::ssprintf("moved %.0f units (%s)", moved, code.c_str()));
  return r;
}

portal2_harness::MacroResult MacroExecutor::PickUp(int mark) {
  portal2_harness::MacroResult r;

  // Phase 1 (main thread): resolve, grabbable-class + reach check, snapshot the
  // pre-grab position. Heap output so a post-cancel closure can't write a dead
  // stack. The class gate stops a wrong mark (e.g. a button) from being
  // +use-pressed as a side effect; the confirmation below is the real "stuck?".
  struct Pre {
    bool ok = false;
    std::string code = "BAD_MARK";
    Vector center{0, 0, 0};
    float reach = 0;  // eye->target distance (3D), for OUT_OF_REACH telemetry
  };
  auto pre = std::make_shared<Pre>();
  bool ran = RunOnMainThreadSync(context_, [pre, mark]() {
    CEntInfo* info = nullptr;
    if (!ResolveMarkInfo(mark, &info, &pre->code)) return;
    if (!IsGrabbableClass(server->GetEntityClassName(info->m_pEntity))) {
      pre->code = "NOT_GRABBABLE";
      return;
    }
    Vector eye;
    if (!PlayerEye(&eye)) {
      pre->code = "NO_PLAYER";
      return;
    }
    Vector center = EntityCenter(SE(info->m_pEntity));
    pre->reach = (center - eye).Length();
    if (pre->reach > kGrabRange) {
      pre->code = "OUT_OF_REACH";
      return;
    }
    pre->center = center;
    pre->ok = true;
  });
  if (!ran) {
    r.set_ok(false);
    r.set_result_code("CANCELLED");
    return r;
  }
  if (!pre->ok) {
    r.set_ok(false);
    r.set_result_code(pre->code);
    if (pre->code == "OUT_OF_REACH")
      r.set_detail(Utils::ssprintf("mark %d out of reach (dist=%.0f, max=%.0f)",
                                   mark, pre->reach, kGrabRange));
    else
      r.set_detail("pick_up could not grab mark " + std::to_string(mark));
    return r;
  }

  // Face the target, then pulse +use to grab it.
  portal2_harness::MacroResult aim = AimAt(mark);
  if (!aim.ok()) return aim;  // BAD_MARK / NO_PLAYER / CANCELLED bubble up
  PulseUse(kSettle);

  // Confirm the grab. There's no networked held flag, so infer it: a real grab
  // ends with the object within kHeldDist of the eye AND snaps it more than
  // kMinGrabMove (independent of view pitch). Requiring the move is what
  // rejects a +use that hit nothing: it reads moved=0 and fails honestly
  // instead of a false success. dz/moved/dist ride in the detail for
  // calibration.
  struct Post {
    bool resolved = false;
    bool held = false;
    std::string code = "BAD_MARK";
    float dz = 0;
    float moved = 0;
    float dist = 0;
  };
  auto post = std::make_shared<Post>();
  Vector preCenter = pre->center;
  bool ran2 = RunOnMainThreadSync(context_, [post, mark, preCenter]() {
    CEntInfo* info = nullptr;
    if (!ResolveMarkInfo(mark, &info, &post->code)) return;
    Vector eye;
    if (!PlayerEye(&eye)) {
      post->code = "NO_PLAYER";
      return;
    }
    Vector center = EntityCenter(SE(info->m_pEntity));
    post->dz = center.z - preCenter.z;
    post->moved = (center - preCenter).Length();
    post->dist = (center - eye).Length();
    post->held = post->dist < kHeldDist && post->moved > kMinGrabMove;
    post->resolved = true;
  });
  if (!ran2) {
    r.set_ok(false);
    r.set_result_code("CANCELLED");
    return r;
  }
  if (!post->resolved) {
    // Stopped resolving between the pulse and the read-back. Surface the honest
    // code, not GRAB_FAILED.
    r.set_ok(false);
    r.set_result_code(post->code);
    r.set_detail("pick_up: grab-confirm read failed (" + post->code + ")");
    return r;
  }
  if (!post->held) {
    r.set_ok(false);
    r.set_result_code("GRAB_FAILED");
    r.set_detail(
        Utils::ssprintf("grab unconfirmed (dz=%.0f moved=%.0f dist=%.0f)",
                        post->dz, post->moved, post->dist));
    return r;
  }

  r.set_ok(true);
  r.set_result_code("SUCCESS");
  r.set_detail(Utils::ssprintf("grabbed mark %d (dz=%.0f moved=%.0f dist=%.0f)",
                               mark, post->dz, post->moved, post->dist));
  return r;
}

portal2_harness::MacroResult MacroExecutor::Release(int mark) {
  // Orient before dropping so the object lands where intended: face the mark if
  // one is given, else look down to drop it at the player's feet (e.g. onto the
  // floor button being stood on). A bad mark just drops at the current facing.
  if (mark > 0) {
    AimAt(mark);
  } else {
    bool ran = RunOnMainThreadSync(context_, []() {
      QAngle cur = engine->GetAngles(Slot());
      ApplyAbsoluteView(QAngle{kReleasePitch, cur.y, 0});
    });
    if (ran) AdvanceTicksBlocking(1);
  }

  // Drop it: a +use pulse releases the carried object. Held-state is tracked by
  // the caller, so there's nothing to confirm and we always report SUCCESS.
  // Caveat: +use is a context toggle -- if the caller issues release while NOT
  // actually holding but standing on a grabbable, the same edge GRABS instead,
  // still reported as "released". The model's held-state tracking is what keeps
  // intent and world-state aligned. Like Wait(), no cancellation hop: on a
  // dropped stream the pulse may still fire (harmless; a Reset reloads the
  // map).
  PulseUse(kSettle);
  portal2_harness::MacroResult r;
  r.set_ok(true);
  r.set_result_code("SUCCESS");
  r.set_detail(mark > 0 ? Utils::ssprintf("released toward mark %d", mark)
                        : "released (look-down)");
  return r;
}

portal2_harness::MacroResult MacroExecutor::Interact(int mark) {
  // Walk into reach, face the mark, pulse +use. The engine decides what +use
  // does from world state (press a button, activate a thing); the verb name is
  // the intent. press will alias this once wired -- identical mechanics.
  portal2_harness::MacroResult nav = GoTo(mark);
  if (!nav.reached()) {
    // Frame a genuine nav failure as a reachability problem, but pass CANCELLED
    // (a dropped stream, which carries no detail by convention) through clean.
    if (nav.result_code() != "CANCELLED")
      nav.set_detail("interact: not in reach (" + nav.detail() + ")");
    return nav;  // carries BLOCKED / STUCK / BAD_MARK / CANCELLED
  }
  portal2_harness::MacroResult aim = AimAt(mark);
  if (!aim.ok()) return aim;
  PulseUse(kSettle);

  portal2_harness::MacroResult r;
  r.set_ok(true);
  r.set_result_code("SUCCESS");
  r.set_detail("interacted with mark " + std::to_string(mark));
  return r;
}
