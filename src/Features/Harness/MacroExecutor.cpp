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
constexpr float kProbeHeight = 18.0f;  // lift the edge ray to ~step height
constexpr float kStepAhead = 24.0f;    // edge ray is cast this far ahead
constexpr float kStepDownMax = 64.0f;  // no floor within this below = edge
// go_to steering: when it stops getting closer, veer to a heading offset from
// the target bearing for a burst, then re-home; give up after a full cycle.
constexpr float kGoToProgressEps = 4.0f;  // min closest-approach gain
constexpr int kGoToStuckBatches = 3;      // no-progress batches before a veer
constexpr int kGoToSteerBatches = 6;      // batches held per veer heading
constexpr float kGoToSteerOffsets[] = {50,  -50, 90,
                                       -90, 140, -140};  // deg off bearing
constexpr int kGoToNumSteerOffsets = 6;

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

// The aiming primitive: clamp pitch to the engine's own clamp, zero roll, set
// the absolute view, and zero the framebulk view delta so the TAS per-tick
// re-apply (GetAngles() - viewAnalog, TasController.cpp:200) preserves it
// (delta 0 => idempotent). Returns the commanded angle (== camera if SetAngles
// survived); if the re-apply ever clobbers it, swap THIS for a viewAnalog
// delta. Main thread only.
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

// Pulse +use, then settle. +use is edge-triggered (PlayerUse acts on the IN_USE
// 0->1 tick), so hold it kUseHoldTicks rather than 1: a 1-tick press can be
// cleared before the simulated tick reads it -- a race vs the tick advance,
// worse under sv_alternateticks' 2-sim-tick pairs. Extra held ticks add no new
// edge, so it stays ONE logical press (can't grab-then-drop). gRPC-thread ONLY:
// it blocks on the tick condvar, so a main-thread closure would self-deadlock.
void PulseUse(int settle) {
  Scheduler::OnMainThread([]() {
    ClearFramebulk();
    tasPlayer->playbackInfo.slots[0].framebulks[0].buttonStates[Use] = true;
  });
  AdvanceTicksBlocking(kUseHoldTicks);
  Scheduler::OnMainThread([]() { ClearFramebulk(); });  // release +use
  AdvanceTicksBlocking(settle);
}

// "EDGE" if the floor drops away kStepAhead in front of the player, else "".
// Walls aren't guarded -- the march slides off them. Main thread.
std::string CheckEdge(void* player, const Vector& feet, float worldYaw) {
  CTraceFilterSimple filter;
  filter.SetPassEntity(player);
  QAngle fwdAng{0, worldYaw, 0};
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

  // March toward the target, sliding off walls; veer to an offset heading when
  // closest-approach stalls, BLOCKED after a full offset cycle gains nothing.
  // Per-batch RunOnMainThreadSync honors cancel; kGoToMaxTicks caps it.
  Vector target = res->center;
  float bestDist = res->initialDist;  // closest 2D approach so far
  float cycleStartBest =
      res->initialDist;  // bestDist when this steer cycle began
  int stuckBatches = 0;  // homing batches without progress
  int steerLeft = 0;     // batches left in the current steer burst
  float steerYaw = 0;    // committed steer heading (world yaw)
  int perturbIdx = 0;    // next offset to try this cycle
  float finalDist = res->initialDist;
  bool reached = false;
  std::string code = "UNREACHABLE";

  struct Sense {
    bool noPlayer = false;
    bool reached = false;
    float dist = 0;
    float homeYaw = 0;
  };
  struct March {
    bool noPlayer = false;
    bool edge = false;
  };

  for (int t = 0; t < kGoToMaxTicks; t += kGoToTickBatch) {
    // Phase A: where are we, how far + which way to the target.
    auto sense = std::make_shared<Sense>();
    bool okA = RunOnMainThreadSync(context_, [sense, target]() {
      ServerEnt* pl = server->GetPlayer(1);
      if (!pl) {
        sense->noPlayer = true;
        return;
      }
      Vector feet = pl->abs_origin();
      Vector forward{target.x - feet.x, target.y - feet.y, 0};
      sense->dist = forward.Length2D();
      if (sense->dist <= kReachRadius) {
        sense->reached = true;
        return;
      }
      Vector up{0, 0, 1};
      QAngle a{0, 0, 0};
      Math::VectorAngles(forward, up, &a);
      sense->homeYaw = a.y;
    });
    if (!okA) {
      r.set_ok(false);
      r.set_result_code("CANCELLED");
      r.set_final_dist(finalDist);
      return r;
    }
    if (sense->noPlayer) {
      code = "NO_PLAYER";
      break;
    }
    finalDist = sense->dist;
    if (sense->reached) {
      reached = true;
      code = "SUCCESS";
      break;
    }

    // Closest-approach is the progress signal (a slide toward the target
    // counts).
    if (sense->dist < bestDist - kGoToProgressEps) {
      bestDist = sense->dist;
      stuckBatches = 0;
      steerLeft = 0;  // made progress -> stop steering, re-home
    } else if (steerLeft == 0) {
      stuckBatches++;
    }

    // Stalled while homing -> veer to the next offset; dead-end after a full
    // cycle.
    if (steerLeft == 0 && stuckBatches >= kGoToStuckBatches) {
      if (perturbIdx >= kGoToNumSteerOffsets) {
        if (bestDist >= cycleStartBest - kGoToProgressEps) {
          code = "BLOCKED";
          break;
        }
        perturbIdx = 0;
        cycleStartBest = bestDist;
      }
      steerYaw = sense->homeYaw + kGoToSteerOffsets[perturbIdx];
      perturbIdx++;
      steerLeft = kGoToSteerBatches;
      stuckBatches = 0;
    }
    float yaw = (steerLeft > 0) ? steerYaw : sense->homeYaw;
    if (steerLeft > 0) steerLeft--;

    // Phase B: don't step off an edge; otherwise aim + walk one batch.
    auto march = std::make_shared<March>();
    bool okB = RunOnMainThreadSync(context_, [march, yaw]() {
      ServerEnt* pl = server->GetPlayer(1);
      if (!pl) {
        march->noPlayer = true;
        return;
      }
      Vector feet = pl->abs_origin();
      if (!CheckEdge(pl, feet, yaw).empty()) {
        march->edge = true;
        ClearFramebulk();  // hold position this batch
        return;
      }
      ApplyAbsoluteView(QAngle{0, yaw, 0});
      SetMoveFramebulk(0, 1);
    });
    if (!okB) {
      r.set_ok(false);
      r.set_result_code("CANCELLED");
      r.set_final_dist(finalDist);
      return r;
    }
    if (march->noPlayer) {
      code = "NO_PLAYER";
      break;
    }
    if (march->edge) {
      // Unsafe heading -> veer to the next offset next batch.
      steerLeft = 0;
      stuckBatches = kGoToStuckBatches;
    }

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
    bool finRan = RunOnMainThreadSync(context_, [fin, feet, target]() {
      ServerEnt* pl = server->GetPlayer(1);
      if (pl) {
        *feet = pl->abs_origin();
        *fin = Vector{target.x - feet->x, target.y - feet->y, 0}.Length2D();
      }
    });
    if (!finRan) {  // stream dropped during settle -- don't claim a result
      r.set_ok(false);
      r.set_result_code("CANCELLED");
      r.set_final_dist(finalDist);
      return r;
    }
    finalDist = *fin;
    finalFeet = *feet;
  }
  float moved =
      Vector{finalFeet.x - res->startFeet.x, finalFeet.y - res->startFeet.y, 0}
          .Length2D();

  // Covered real ground but didn't arrive -> ADVANCED, so the model re-plans
  // from the new spot instead of repeating the verb.
  bool advanced = !reached && moved > kReachRadius &&
                  (code == "BLOCKED" || code == "UNREACHABLE");
  r.set_ok(reached || advanced);
  r.set_result_code(advanced ? "ADVANCED" : code);
  r.set_reached(reached);
  r.set_final_dist(finalDist);
  r.set_moved_dist(moved);
  if (advanced)
    r.set_detail(Utils::ssprintf("advanced %.0f units, %.0f to go (%s)", moved,
                                 finalDist, code.c_str()));
  else
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
      std::string g = CheckEdge(pl, feet, worldYaw);
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

  // Phase 1 (main thread): resolve + grabbable-class + reach check, snapshot
  // the pre-grab pos. Heap output survives a post-cancel closure run; the class
  // gate stops a wrong mark (e.g. a button) being +use-pressed by a bad grab.
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

  // No networked held flag, so infer the grab: the object ends within kHeldDist
  // of the eye AND moved more than kMinGrabMove. The move check rejects a +use
  // that hit nothing (reads moved=0) instead of reporting a false success.
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
  // Orient before dropping: face the mark if given, else look down to drop at
  // the player's feet (e.g. onto a floor button being stood on).
  if (mark > 0) {
    AimAt(mark);
  } else {
    bool ran = RunOnMainThreadSync(context_, []() {
      QAngle cur = engine->GetAngles(Slot());
      ApplyAbsoluteView(QAngle{kReleasePitch, cur.y, 0});
    });
    if (ran) AdvanceTicksBlocking(1);
  }

  // +use pulse drops the carried object. The caller tracks held-state, so
  // there's nothing to confirm -- always SUCCESS. Caveat: +use is a context
  // toggle, so a release issued while NOT holding (but stood on a grabbable)
  // GRABS instead; the caller's held-tracking keeps intent aligned. No cancel
  // hop (like Wait): on a dropped stream the pulse may still fire (harmless).
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
