#include "MacroExecutor.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_set>

#include "Command.hpp"
#include "Entity.hpp"
#include "Event.hpp"
#include "Features/EntityList.hpp"
#include "Features/Tas/TasController.hpp"
#include "Features/Tas/TasPlayer.hpp"
#include "Features/Tas/TasScript.hpp"
#include "GoToPlanner.hpp"
#include "Harness.hpp"
#include "HarnessThread.hpp"
#include "MarkTable.hpp"
#include "Modules/Console.hpp"
#include "Modules/Engine.hpp"
#include "Modules/Server.hpp"
#include "Offsets.hpp"
#include "Scheduler.hpp"
#include "Utils.hpp"
#include "Utils/Math.hpp"
#include "Utils/SDK/EntityEdict.hpp"
#include "Utils/SDK/Trace.hpp"
#include "Variable.hpp"

Variable sar_harness_goto_debug(
    "sar_harness_goto_debug", "0",
    "Log per-batch go_to VFH steering (heading, clearance, feet move).\n");

namespace {

// wait() upper bound (~10s @ 60fps): a verb must not freeze the world forever.
constexpr int kMaxWaitTicks = 600;
// Source view-pitch limit. The TAS re-apply clamps to the cl_pitch* cvars
// regardless; this just keeps look()'s requested angle sane before SetAngles.
constexpr float kPitchLimit = 89.0f;

// go_to / move march tuning (flat chambers).
constexpr int kGoToTickBatch = 4;   // ticks advanced per march iteration
constexpr int kGoToMaxTicks = 400;  // give up after this many ticks
constexpr int kGoToSettle = 24;  // ticks to bleed off walk velocity at the end
constexpr float kReachRadius = 48.0f;  // horizontal dist that counts as arrived
constexpr float kLegRadius = 24.0f;    // looser arrival for an A* waypoint
constexpr int kMaxReplans = 2;     // A* re-plans on a dynamically-blocked leg
constexpr float kStuckEps = 1.0f;  // <this much progress/iter twice = stuck
constexpr float kProbeHeight = 18.0f;  // lift the edge ray to ~step height
constexpr float kStepAhead = 24.0f;    // edge ray is cast this far ahead
constexpr float kStepDownMax = 64.0f;  // no floor within this below = edge
// go_to VFH steering (see ChooseVfhHeading) + global-stall termination.
constexpr float kGoToProgressEps = 4.0f;  // min closest-approach gain
constexpr int kVfhBins = 24;              // clearance rays around the circle
constexpr float kVfhProbeDist = 96.0f;    // per-ray clearance horizon (units)
constexpr float kVfhClearMin = 40.0f;     // a bin is passable at/above this
constexpr float kVfhClearWeight = 0.15f;  // clearance bonus vs goal-angle cost
constexpr float kVfhHystBonus = 15.0f;    // deg-equiv bias to hold last heading
constexpr int kGoToGlobalStall = 40;      // no-progress batches -> BLOCKED
// Wedge detector: a move the point-ray says is clear but the feet don't take
// (rim/lip/hull-clip the ray misses) blocks that heading for a cooldown, so the
// next pick veers/backs out instead of pushing into it forever.
constexpr float kWedgeEps = 3.0f;  // batch feet-move below this = stuck
constexpr int kWedgeStuckBatches =
    2;                              // no-move batches before a heading blocks
constexpr int kWedgeCooldown = 16;  // batches a wedged heading stays blocked

// Interaction-verb tuning (pick_up / release / interact).
constexpr int kSettle = 20;       // ticks to let a grab/drop/use resolve
constexpr int kUseHoldTicks = 3;  // hold +use past the press/tick-advance race
constexpr float kGrabRange = 96.0f;   // reach gate: ~80u radius + half a cube
constexpr float kHeldDist = 80.0f;    // a held object rides within this of eye
constexpr float kMinGrabMove = 8.0f;  // a real grab snaps it more than this
constexpr float kReleasePitch = 75.0f;  // mark-less release: look-down pitch

// Place-on-button seat-find tuning.
constexpr float kSeatProbeUp =
    24.0f;  // down-trace starts this far over the button OBB top
constexpr float kSeatProbeDown =
    16.0f;  // ...and ends this far under its OBB bottom
constexpr float kSeatBias =
    1.0f;  // sink the cube bottom this far into the press volume
constexpr float kStandoffMargin =
    8.0f;  // gap past the button footprint for a stepped-off player to stand
constexpr int kStandoffBearings = 16;  // directions probed around the button
constexpr float kStandoffLift =
    2.0f;  // lift the player-fit hull test off the floor
constexpr float kPressNormalMin =
    0.7f;  // press surface must face up at least this much (1 = flat)
constexpr float kCorridorSlack =
    8.0f;  // drop sweep may settle at most this far above the seat
constexpr float kEyeLineSlack =
    8.0f;  // eye->seat ray may stop at most this short of the seat
constexpr float kOccupancyLift =
    2.0f;  // raise the seat-occupancy hull this far clear of the button

int Slot() { return GET_SLOT(); }

// index<<16 | serial (MarkTable convention), to match entities by identity.
uint32_t PackEntKey(int index, uint16_t serial) {
  return (static_cast<uint32_t>(index) << 16) | serial;
}

// Cube held by the last pick_up (0 = none); go_to skips it as an obstacle.
std::atomic<uint32_t> g_heldEntityKey{0};

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

// The cube the last pick_up grabbed -> live ServerEnt, serial-revalidated so a
// recycled slot can't resolve to a stranger. null if hands are empty or stale.
// Main thread only.
ServerEnt* HeldCube() {
  uint32_t key = g_heldEntityKey.load();
  if (!key) return nullptr;
  int index = static_cast<int>(key >> 16);
  uint16_t serial = static_cast<uint16_t>(key & 0xFFFF);
  CEntInfo* info = entityList->GetEntityInfoByIndex(index);
  if (!info || !info->m_pEntity ||
      static_cast<uint16_t>(info->m_SerialNumber) != serial)
    return nullptr;
  return SE(info->m_pEntity);
}

// Where a held cube should land to rest on a button's press surface, with its
// orientation preserved. A press is pure bbox-overlap of the button's trigger,
// so the recipe is geometric: trace down through the button centre to read the
// real collision plane, sit the cube on it (bottom a hair into the trigger),
// keep its current orientation. Read-only -- traces + field reads, mutates
// nothing. Main thread only (live entities).
struct Seat {
  bool ok;  // false => no solid press surface found under the button
  Vector
      origin;  // target m_vecAbsOrigin (CBaseEntity::Teleport takes an origin)
  QAngle angles;   // cube orientation, carried verbatim
  Vector center;   // world point the cube centre lands on
  Vector surface;  // press surface the down-trace hit
  float normalZ;   // up-component of that surface (1 = flat, ~0 = wall)
};
Seat ComputeSeat(ServerEnt* button, ServerEnt* cube) {
  Seat s{};
  ICollideable& bcoll = button->collision();
  Vector bOrigin = button->abs_origin();

  // X/Y: midpoint of the button's trigger volume; if the vfunc hands back a
  // degenerate box, fall through to the prop origin (these buttons are
  // axis-aligned, so the two share an X/Y centre anyway).
  Vector tmin = bOrigin, tmax = bOrigin;
  bcoll.WorldSpaceTriggerBounds(&tmin, &tmax);
  float cx = bOrigin.x, cy = bOrigin.y;
  if (tmax.x >= tmin.x && tmax.y >= tmin.y) {
    cx = (tmin.x + tmax.x) * 0.5f;
    cy = (tmin.y + tmax.y) * 0.5f;
  }

  // Z: a point ray straight down through that centre reads the real press
  // plane (its height and up-ness), so a recessed socket or a flush button is
  // handled by the trace rather than by trusting the prop OBB top.
  Vector bmin = bcoll.OBBMins(), bmax = bcoll.OBBMaxs();
  Vector start{cx, cy, bOrigin.z + bmax.z + kSeatProbeUp};
  QAngle down{90, 0, 0};
  float span = (bmax.z - bmin.z) + kSeatProbeUp + kSeatProbeDown;
  CTraceFilterSimple filter;
  filter.SetPassEntity(server->GetPlayer(1));
  CGameTrace tr;
  if (!engine->Trace(start, down, span, MASK_PLAYERSOLID, filter, tr)) {
    s.ok = false;
    return s;
  }
  s.surface = tr.endpos;
  s.normalZ = tr.plane.normal.z;

  // Cube world-space Z half-extent and its centre->origin offset, both from one
  // collision-to-world matrix so a tilted (reflector) cube stays consistent.
  ICollideable& ccoll = cube->collision();
  Vector cmin = ccoll.OBBMins(), cmax = ccoll.OBBMaxs();
  Vector localCenter = (cmin + cmax) * 0.5f;
  Vector localExtents = cmax - localCenter;
  matrix3x4_t m = ccoll.CollisionToWorldTransform();
  Vector cubeCenter = m.VectorTransform(localCenter);
  const float* zrow = m.m_flMatVal[2];
  float halfH = std::fabs(localExtents.x * zrow[0]) +
                std::fabs(localExtents.y * zrow[1]) +
                std::fabs(localExtents.z * zrow[2]);

  // Seat the cube centre over the press point with its bottom kSeatBias into
  // the trigger (overlap latches the press; resting a hair above never would).
  s.center = Vector{cx, cy, tr.endpos.z + halfH - kSeatBias};
  s.origin = s.center + (cube->abs_origin() - cubeCenter);
  s.angles = cube->abs_angles();
  s.ok = true;
  return s;
}

// A spot just off a button where the player hull fits on solid floor, taken
// from the first of a ring of bearings around it. Used to step the player off a
// button it is standing on so a held cube can take its place. Read-only --
// traces only. Main thread (live entities).
bool FindPlayerStandoff(ServerEnt* button, ServerEnt* player, Vector* out) {
  ICollideable& bcoll = button->collision();
  Vector bmin = bcoll.OBBMins(), bmax = bcoll.OBBMaxs();
  ICollideable& pcoll = player->collision();
  Vector pmin = pcoll.OBBMins(), pmax = pcoll.OBBMaxs();
  float buttonHalfW =
      0.5f * Vector{bmax.x - bmin.x, bmax.y - bmin.y, 0}.Length2D();
  float playerHalfW =
      0.5f * Vector{pmax.x - pmin.x, pmax.y - pmin.y, 0}.Length2D();
  float ringR = buttonHalfW + playerHalfW + kStandoffMargin;

  Vector bOrigin = button->abs_origin();
  float feetZ = player->abs_origin().z;
  CTraceFilterSimple filter;
  filter.SetPassEntity(player);
  for (int i = 0; i < kStandoffBearings; ++i) {
    QAngle a{0, (360.0f / kStandoffBearings) * i, 0};
    Vector dir;
    Math::AngleVectors(a, &dir);
    float cx = bOrigin.x + dir.x * ringR, cy = bOrigin.y + dir.y * ringR;
    // Floor under the candidate?
    Vector top{cx, cy, feetZ + kProbeHeight};
    QAngle down{90, 0, 0};
    CGameTrace floorTr;
    if (!engine->Trace(top, down, kProbeHeight + kStepDownMax, MASK_PLAYERSOLID,
                       filter, floorTr))
      continue;
    // Player hull fits there? (a hull inside a wall reads startsolid)
    Vector at{cx, cy, floorTr.endpos.z + kStandoffLift};
    CGameTrace hullTr;
    if (engine->TraceHull(at, at, pmin, pmax, MASK_PLAYERSOLID, filter, hullTr))
      continue;
    *out = Vector{cx, cy, floorTr.endpos.z};
    return true;
  }
  return false;
}

// Trace filter that ignores two entities (e.g. the placer and the cube being
// placed) so neither registers as a blocker in the drop path or at the seat.
struct TraceSkip2 : public CTraceFilterSimple {
  const void* skip2 = nullptr;
  bool ShouldHitEntity(void* ent, int mask) override {
    return CTraceFilterSimple::ShouldHitEntity(ent, mask) && ent != skip2;
  }
};

// Whether placing the held cube on this seat is something a clean hand-drop
// from where the player stands could have done -- a battery of static traces,
// each a prove-or-refuse gate. selfOnSeat is the one occupant that stays fair:
// the player is in the seat itself, which a displacement step clears. All
// read-only -- traces + field reads. Main thread (live entities).
struct Fairness {
  bool reach = false;        // eye close enough to the seat to have placed it
  bool pressNormal = false;  // press surface faces up, not a wall/steep button
  bool corridor = false;     // a drop from above reaches the seat unobstructed
  bool eyeLine = false;      // clear line of sight from the eye to the seat
  bool seatClear = false;    // nothing foreign already in the seat
  bool selfOnSeat = false;   // the only seat occupant is the player (displace)
  bool fair = false;
  float reachDist = 0;    // eye -> seat distance
  float corridorGap = 0;  // how far above the seat the drop sweep settled
  float eyeGap = 0;       // eye -> seat distance still uncovered at a block
  std::string occupant;   // seat occupant class (empty if clear/self)
  std::string eyeHit;     // eye-line blocker class (empty if clear)
};
Fairness CheckFairness(ServerEnt* button, ServerEnt* cube, ServerEnt* player,
                       const Seat& seat) {
  Fairness f;
  const void* pl = player;
  const void* cu = cube;
  const void* bt = button;

  Vector eye;
  PlayerEye(&eye);

  // 1. Reach: the player must already be next to the seat (no cross-room snap).
  f.reachDist = (seat.center - eye).Length();
  f.reach = f.reachDist <= kGrabRange;

  // 2. Press surface faces up -- a wall or steep button can't hold a dropped
  // cube even though the bbox would technically overlap the trigger.
  f.pressNormal = seat.normalZ > kPressNormalMin;

  ICollideable& ccoll = cube->collision();
  Vector cmin = ccoll.OBBMins(), cmax = ccoll.OBBMaxs();
  float cubeH = cmax.z - cmin.z;

  // 3. Drop corridor: sweep the cube hull straight down from one cube-height
  // above the seat, skipping the player and the cube. It must settle on the
  // press surface, not catch on geometry/grate/fizzler in the path.
  {
    TraceSkip2 filter;
    filter.SetPassEntity(pl);
    filter.skip2 = cu;
    Vector top = seat.center + Vector{0, 0, cubeH}, bot = seat.center;
    CGameTrace tr;
    engine->TraceHull(top, bot, cmin, cmax, MASK_PLAYERSOLID, filter, tr);
    f.corridorGap = tr.endpos.z - seat.center.z;
    f.corridor = !tr.startsolid && f.corridorGap <= kCorridorSlack;
  }

  // 4. Seat occupancy: a zero-length cube hull resting ON the press surface --
  // lifted clear of the button the seat dips into, so the button it sits on
  // never reads as the occupant. The player in the seat is self (still fair --
  // displace it off); any other solid is a foreign occupant we won't disturb.
  {
    TraceSkip2 filter;
    filter.SetPassEntity(bt);
    filter.skip2 = cu;
    Vector occ = seat.center + Vector{0, 0, kSeatBias + kOccupancyLift};
    CGameTrace tr;
    engine->TraceHull(occ, occ, cmin, cmax, MASK_PLAYERSOLID, filter, tr);
    if (!tr.startsolid) {
      f.seatClear = true;
    } else {
      const char* oc =
          tr.m_pEnt ? server->GetEntityClassName(tr.m_pEnt) : "world";
      f.occupant = oc ? oc : "world";
      f.selfOnSeat = f.occupant == "player";
    }
  }

  // 5. Eye line: a clear ray from the eye to the seat, skipping the player and
  // the cube, rejects "in reach but walled off behind glass".
  {
    TraceSkip2 filter;
    filter.SetPassEntity(pl);
    filter.skip2 = cu;
    Vector d = seat.center - eye, up{0, 0, 1};
    float dist = d.Length();
    QAngle ang{0, 0, 0};
    Math::VectorAngles(d, up, &ang);
    Vector from = eye;
    CGameTrace tr;
    bool hit = engine->Trace(from, ang, dist, MASK_PLAYERSOLID, filter, tr);
    f.eyeGap = hit ? (1.0f - tr.fraction) * dist : 0.0f;
    f.eyeLine = !hit || f.eyeGap <= kEyeLineSlack;
    if (!f.eyeLine)
      f.eyeHit = tr.m_pEnt ? server->GetEntityClassName(tr.m_pEnt) : "world";
  }

  f.fair = f.reach && f.pressNormal && f.corridor && f.eyeLine &&
           (f.seatClear || f.selfOnSeat);
  return f;
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

// Pulse +use (drop / grab / activate), HOLDING `view` across the whole pulse.
// +use is edge-triggered (PlayerUse acts on the IN_USE 0->1 tick), so hold it
// kUseHoldTicks rather than 1: a 1-tick press can be cleared before the
// simulated tick reads it -- a race vs the tick advance, worse under
// sv_alternateticks' 2-sim-tick pairs. Extra held ticks add no new edge, so it
// stays ONE logical press (can't grab-then-drop). The view is re-asserted every
// batch through the settle: a single SetAngles drifts -- the engine ratchets
// pitch to the ceiling each tick -- exactly why the go_to march re-asserts per
// batch. Without it the held object swings up mid-pulse and drops wrong.
// gRPC-thread ONLY: it blocks on the tick condvar, so a main-thread closure
// would self-deadlock.
void PulseUse(int settle, QAngle view) {
  Scheduler::OnMainThread([view]() {
    ApplyAbsoluteView(view);  // clears the framebulk, so arm +use AFTER
    tasPlayer->playbackInfo.slots[0].framebulks[0].buttonStates[Use] = true;
  });
  AdvanceTicksBlocking(kUseHoldTicks);
  // Release +use (the first re-assert's ClearFramebulk drops it) and settle,
  // holding the view each batch so the drop lands where we aimed.
  for (int done = 0; done < settle; done += kGoToTickBatch) {
    int batch = std::min(kGoToTickBatch, settle - done);
    Scheduler::OnMainThread([view]() { ApplyAbsoluteView(view); });
    AdvanceTicksBlocking(batch);
  }
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

// Open distance along world yaw from `from`, capped at maxDist (point ray).
// Main thread.
float RayClearance(void* player, const Vector& from, float worldYaw,
                   float maxDist) {
  CTraceFilterSimple filter;
  filter.SetPassEntity(player);
  Vector pos = from;
  QAngle ang{0, worldYaw, 0};
  CGameTrace tr;
  if (!engine->Trace(pos, ang, maxDist, MASK_PLAYERSOLID, filter, tr))
    return maxDist;
  return tr.fraction * maxDist;
}

// World yaw -> nearest VFH bin index in [0, kVfhBins).
int VfhBin(float yaw) {
  constexpr float binDeg = 360.0f / kVfhBins;
  int b = static_cast<int>(std::lround(yaw / binDeg)) % kVfhBins;
  return b < 0 ? b + kVfhBins : b;
}

// Props go_to routes around instead of shoving: cubes, boxes, turrets, buttons.
bool IsGoToObstacleClass(const char* cls) {
  if (!cls) return false;
  return !std::strcmp(cls, "prop_weighted_cube") ||
         !std::strcmp(cls, "prop_monster_box") ||
         !std::strcmp(cls, "npc_portal_turret_floor") ||
         !std::strcmp(cls, "prop_floor_button") ||
         !std::strcmp(cls, "prop_floor_cube_button") ||
         !std::strcmp(cls, "prop_floor_ball_button") ||
         !std::strcmp(cls, "prop_under_floor_button") ||
         !std::strcmp(cls, "prop_button");
}

// Lower each VFH bin's clearance to the free distance toward any obstacle prop
// covering that bearing (footprint circle + player half-width), so go_to keeps
// a >=kVfhClearMin standoff. Skips the target + held cube. Main thread.
void InjectObstacles(float* clear, const Vector& feet, float playerHalfWidth,
                     uint32_t targetKey, uint32_t heldKey) {
  constexpr float binDeg = 360.0f / kVfhBins;
  for (int i = 0; i < Offsets::NUM_ENT_ENTRIES; ++i) {
    CEntInfo* info = entityList->GetEntityInfoByIndex(i);
    if (!info || !info->m_pEntity) continue;
    uint32_t key = PackEntKey(i, static_cast<uint16_t>(info->m_SerialNumber));
    if (key == targetKey || key == heldKey) continue;
    if (!IsGoToObstacleClass(server->GetEntityClassName(info->m_pEntity)))
      continue;

    ServerEnt* se = SE(info->m_pEntity);
    ICollideable& coll = se->collision();
    Vector mins = coll.OBBMins(), maxs = coll.OBBMaxs();
    Vector center = se->abs_origin() + (mins + maxs) * 0.5f;
    Vector d{center.x - feet.x, center.y - feet.y, 0};
    float dist = d.Length2D();
    float radius =  // footprint circle (rotation-safe) + player body
        0.5f * Vector{maxs.x - mins.x, maxs.y - mins.y, 0}.Length2D() +
        playerHalfWidth;
    float freeDist = std::max(0.0f, dist - radius);
    float halfWidth =  // overlap -> block the whole 90 deg arc toward it
        (dist > radius) ? RAD2DEG(std::asin(radius / dist)) : 90.0f;
    int span = static_cast<int>(std::ceil(halfWidth / binDeg));
    int centerBin = VfhBin(RAD2DEG(std::atan2(d.y, d.x)));
    for (int k = -span; k <= span; ++k) {
      int b = ((centerBin + k) % kVfhBins + kVfhBins) % kVfhBins;
      if (freeDist < clear[b]) clear[b] = freeDist;
    }
  }
}

struct VfhPick {
  bool found = false;   // false => every heading is boxed or a cliff this batch
  float yaw = 0;        // world heading the body should strafe toward
  float clearance = 0;  // chosen bin's open distance (debug telemetry)
};

// 360 deg VFH: one clearance ray per bin; pick the highest-scoring passable,
// non-cliff bin. Score = -|angle-to-goal| (dominant) + clearance + hysteresis.
// Bins flagged in wedgeTtl (physically stuck, ray-invisible) are skipped.
// Main thread.
VfhPick ChooseVfhHeading(void* player, const Vector& feet, float goalBearing,
                         float lastYaw, bool haveLast, const int* wedgeTtl,
                         float playerHalfWidth, uint32_t targetKey,
                         uint32_t heldKey) {
  constexpr float binDeg = 360.0f / kVfhBins;
  Vector probe = feet + Vector{0, 0, kProbeHeight};
  float clear[kVfhBins];
  for (int i = 0; i < kVfhBins; i++)
    clear[i] = RayClearance(player, probe, i * binDeg, kVfhProbeDist);
  // Analytic obstacle footprints (props the rays would shove) on top of the ray
  // clearances, so the chosen valley routes around them.
  InjectObstacles(clear, feet, playerHalfWidth, targetKey, heldKey);

  for (int guard = 0; guard < kVfhBins; guard++) {
    int best = -1;
    float bestScore = 0;
    for (int i = 0; i < kVfhBins; i++) {
      if (clear[i] < kVfhClearMin || wedgeTtl[i] > 0) continue;
      float yaw = i * binDeg;
      float s = -std::fabs(NormalizeYaw(yaw - goalBearing)) +
                kVfhClearWeight * clear[i];
      if (haveLast && std::fabs(NormalizeYaw(yaw - lastYaw)) < binDeg * 0.5f)
        s += kVfhHystBonus;
      if (best < 0 || s > bestScore) {
        best = i;
        bestScore = s;
      }
    }
    if (best < 0) return {};  // fully boxed
    float yaw = best * binDeg;
    if (CheckEdge(player, feet, yaw).empty()) return {true, yaw, clear[best]};
    clear[best] = 0;  // cliff -> drop this bin and re-pick
  }
  return {};
}

// One VFH march leg's result. GoTo turns it into the MacroResult; the A* layer
// will chain legs and decrement the tick budget across them.
struct MarchOutcome {
  std::string code = "BLOCKED";  // SUCCESS / BLOCKED / NO_PLAYER (cap->BLOCKED)
  bool reached = false;
  bool cancelled = false;  // stream dropped mid-march; caller skips settle
  float dist = 0;          // 2D dist to target at loop exit (pre-settle)
  int ticksUsed = 0;       // ticks advanced; the cross-leg budget decrement
};

// One VFH march leg: strafe the body toward the freest goal-ward heading with
// the camera held on `target`, advancing ticks in batches until arrival, a
// global stall, the tick budget, or a dropped stream. Per-leg state (wedge ttl,
// bestDist, lastYaw, stallBatches) starts fresh here, so chained legs don't
// bleed stall into each other. The caller owns the post-march settle + result.
MarchOutcome MarchTo(grpc::ServerContext* context, const Vector& target,
                     float reachRadius, int tickBudget, uint32_t targetKey,
                     uint32_t heldKey, const Vector& startFeet,
                     float initialDist) {
  MarchOutcome out;
  out.dist = initialDist;
  float bestDist = initialDist;  // closest 2D approach so far (global)
  int stallBatches = 0;          // batches since bestDist last improved
  float lastYaw = 0;             // committed heading (hysteresis)
  bool haveLast = false;

  struct Step {
    bool noPlayer = false;
    bool reached = false;
    bool commandedMove = false;  // a strafe was issued (vs a boxed hold)
    float dist = 0;
    float chosenYaw = 0;
    Vector feet{0, 0, 0};  // for per-batch displacement
  };
  struct WedgeState {
    int ttl[kVfhBins] = {};  // per-bin physical-block countdown
    int stuckRun = 0;  // consecutive no-move batches on a committed heading
  };

  auto wedge = std::make_shared<WedgeState>();
  Vector prevFeet = startFeet;
  bool lastCommandedMove = false;
  int t = 0;
  for (; t < tickBudget; t += kGoToTickBatch) {
    auto step = std::make_shared<Step>();
    bool ok = RunOnMainThreadSync(
        context, [step, target, lastYaw, haveLast, lastCommandedMove, prevFeet,
                  wedge, t, bestDist, targetKey, heldKey, reachRadius]() {
          ServerEnt* pl = server->GetPlayer(1);
          if (!pl) {
            step->noPlayer = true;
            return;
          }
          Vector feet = pl->abs_origin();
          step->feet = feet;
          Vector forward{target.x - feet.x, target.y - feet.y, 0};
          step->dist = forward.Length2D();
          if (step->dist <= reachRadius) {
            step->reached = true;
            return;
          }
          // Wedge feedback: decay blocks, and if last batch commanded a move
          // the feet didn't take (a rim/lip/hull-clip the ray missed), block
          // that heading (+/-1 bin: the hull is wider than a ray) so this pick
          // veers off it.
          float moved =
              Vector{feet.x - prevFeet.x, feet.y - prevFeet.y, 0}.Length2D();
          for (int i = 0; i < kVfhBins; i++)
            if (wedge->ttl[i] > 0) wedge->ttl[i]--;
          if (lastCommandedMove) {
            wedge->stuckRun = (moved < kWedgeEps) ? wedge->stuckRun + 1 : 0;
            if (wedge->stuckRun >= kWedgeStuckBatches) {
              int b = VfhBin(lastYaw);
              wedge->ttl[b] = wedge->ttl[(b + 1) % kVfhBins] =
                  wedge->ttl[(b + kVfhBins - 1) % kVfhBins] = kWedgeCooldown;
              wedge->stuckRun =
                  0;  // give the next heading its own grace window
            }
          }
          Vector up{0, 0, 1};
          QAngle a{0, 0, 0};
          Math::VectorAngles(forward, up, &a);
          float goalBearing = a.y;
          step->chosenYaw = goalBearing;
          Vector pmax = pl->collision().OBBMaxs();
          float playerHalfWidth = std::max(pmax.x, pmax.y);
          VfhPick pick =
              ChooseVfhHeading(pl, feet, goalBearing, lastYaw, haveLast,
                               wedge->ttl, playerHalfWidth, targetKey, heldKey);
          // Camera on the target; ApplyAbsoluteView clears the framebulk, so a
          // boxed batch (no pick) holds.
          ApplyAbsoluteView(QAngle{0, goalBearing, 0});
          if (pick.found) {
            step->chosenYaw = pick.yaw;
            step->commandedMove = true;
            float delta = DEG2RAD(NormalizeYaw(pick.yaw - goalBearing));
            SetMoveFramebulk(-std::sin(delta), std::cos(delta));  // body-frame
          }
          if (sar_harness_goto_debug.GetBool()) {
            int wb = 0;
            for (int i = 0; i < kVfhBins; i++)
              if (wedge->ttl[i] > 0) wb++;
            console->Print(
                "goto t=%d dist=%.0f best=%.0f hdg=%+.0f clr=%.0f moved=%.1f "
                "wb=%d%s\n",
                t, step->dist, bestDist,
                NormalizeYaw(step->chosenYaw - goalBearing), pick.clearance,
                moved, wb, pick.found ? "" : " BOXED");
          }
        });
    if (!ok) {
      out.cancelled = true;
      out.ticksUsed = t;
      return out;  // out.dist carries the last-good finalDist
    }
    if (step->noPlayer) {
      out.code = "NO_PLAYER";
      break;
    }
    out.dist = step->dist;
    if (step->reached) {
      out.reached = true;
      out.code = "SUCCESS";
      break;
    }
    // Regress is allowed (backing out of a pocket), so only a long global stall
    // -- not one bad batch -- blocks.
    if (step->dist < bestDist - kGoToProgressEps) {
      bestDist = step->dist;
      stallBatches = 0;
    } else if (++stallBatches >= kGoToGlobalStall) {
      out.code = "BLOCKED";
      break;
    }
    lastYaw = step->chosenYaw;
    haveLast = true;
    lastCommandedMove = step->commandedMove;
    prevFeet = step->feet;

    AdvanceTicksBlocking(kGoToTickBatch);
  }
  out.ticksUsed = t;
  return out;
}

// After a straight march BLOCKED, route around the pocket with A*: plan over
// the lazy hull-probed grid, then march each waypoint leg through the same
// MarchTo. Intermediate waypoints use a loose arrival radius; the final leg
// targets the real mark. A dynamically-blocked leg (a door/cube that moved)
// re-plans from the current feet, capped at kMaxReplans. A fresh kGoToMaxTicks
// budget is shared across the legs so a long route can't run unbounded.
// gRPC-thread only.
MarchOutcome RouteAround(grpc::ServerContext* context, const Vector& target,
                         uint32_t targetKey, uint32_t heldKey,
                         float initialDist) {
  MarchOutcome out;  // defaults to BLOCKED
  out.dist = initialDist;
  int budget = kGoToMaxTicks;

  for (int attempt = 0; attempt <= kMaxReplans; ++attempt) {
    // Read feet + plan a route on the main thread (the planner traces the
    // world).
    auto legs = std::make_shared<std::vector<Vector>>();
    auto startFeet = std::make_shared<Vector>();
    bool ran = RunOnMainThreadSync(context, [legs, startFeet, target, targetKey,
                                             heldKey]() {
      ServerEnt* pl = server->GetPlayer(1);
      if (!pl) return;
      *startFeet = pl->abs_origin();
      GoToPlanner planner(pl->collision().OBBMins(), pl->collision().OBBMaxs(),
                          startFeet->z, targetKey, heldKey);
      *legs = planner.Plan(*startFeet, target);
    });
    if (!ran) {
      out.cancelled = true;
      return out;
    }
    if (legs->empty()) return out;  // no route exists -> BLOCKED

    bool blockedLeg = false;
    Vector from = *startFeet;
    for (size_t i = 0; i < legs->size(); ++i) {
      bool last = (i + 1 == legs->size());
      Vector legTarget = last ? target : (*legs)[i];
      float radius = last ? kReachRadius : kLegRadius;
      float d =
          Vector{legTarget.x - from.x, legTarget.y - from.y, 0}.Length2D();
      MarchOutcome leg = MarchTo(context, legTarget, radius, budget, targetKey,
                                 heldKey, from, d);
      if (leg.cancelled) {
        out.cancelled = true;
        return out;
      }
      budget -= leg.ticksUsed;
      out.dist = leg.dist;
      from = legTarget;
      if (leg.code == "NO_PLAYER") {
        out.code = "NO_PLAYER";
        return out;
      }
      if (last && leg.reached) {
        out.reached = true;
        out.code = "SUCCESS";
        return out;
      }
      if (leg.code == "BLOCKED") {  // dynamic block -> re-plan from here
        blockedLeg = true;
        break;
      }
      if (budget <= 0) return out;  // budget spent -> BLOCKED
    }
    if (!blockedLeg) break;  // all legs marched, final not reached -> BLOCKED
  }
  return out;
}

}  // namespace

// Hands start empty each episode; a stale key would match the reloaded cube.
ON_EVENT(SESSION_START) { g_heldEntityKey = 0; }

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
    uint32_t targetKey = 0;  // skip the destination in the obstacle histogram
  };
  auto res = std::make_shared<Resolve>();
  bool ran = RunOnMainThreadSync(context_, [res, mark]() {
    if (!ResolveMarkCenter(mark, &res->center, &res->code)) return;
    res->ok = true;
    auto [idx, ser] = markTable.GetEntityFromMark(mark);
    if (idx >= 0) res->targetKey = PackEntKey(idx, static_cast<uint16_t>(ser));
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

  // VFH march to the resolved target. targetKey/heldKey skip the destination
  // and any carried cube in the obstacle histogram. The per-batch march loop
  // lives in MarchTo, so the A* layer can chain legs through the same
  // executor.
  Vector target = res->center;
  uint32_t heldKey = g_heldEntityKey.load();
  MarchOutcome m =
      MarchTo(context_, target, kReachRadius, kGoToMaxTicks, res->targetKey,
              heldKey, res->startFeet, res->initialDist);
  // Straight march stalled in a pocket -> route around it with A* (continues
  // from the blocked feet, so the plan stays short and well under the cell
  // cap).
  if (!m.cancelled && m.code == "BLOCKED")
    m = RouteAround(context_, target, res->targetKey, heldKey, m.dist);
  if (m.cancelled) {
    r.set_ok(false);
    r.set_result_code("CANCELLED");
    r.set_final_dist(m.dist);
    return r;
  }
  bool reached = m.reached;
  std::string code = m.code;
  float finalDist = m.dist;

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
  bool advanced = !reached && moved > kReachRadius && code == "BLOCKED";
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

  // Face the target, then pulse +use to grab it (holding the aim across the
  // pulse so the view doesn't drift to the ceiling mid-grab).
  portal2_harness::MacroResult aim = AimAt(mark);
  if (!aim.ok()) return aim;  // BAD_MARK / NO_PLAYER / CANCELLED bubble up
  PulseUse(kSettle, QAngle{aim.aim_pitch(), aim.aim_yaw(), 0});

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

  // Cache the carried cube so go_to skips it (else it self-blocks the march).
  auto [hidx, hser] = markTable.GetEntityFromMark(mark);
  if (hidx >= 0)
    g_heldEntityKey = PackEntKey(hidx, static_cast<uint16_t>(hser));

  r.set_ok(true);
  r.set_result_code("SUCCESS");
  r.set_detail(Utils::ssprintf("grabbed mark %d (dz=%.0f moved=%.0f dist=%.0f)",
                               mark, post->dz, post->moved, post->dist));
  return r;
}

portal2_harness::MacroResult MacroExecutor::Release(int mark) {
  g_heldEntityKey = 0;  // hands empty (self-corrects on next pick_up if wrong)

  // Orient before dropping: face the mark if given, else look down to drop at
  // the player's feet (e.g. onto a floor button being stood on). Capture the
  // view so PulseUse can HOLD it across the drop -- a single SetAngles drifts
  // pitch to the ceiling and flings the held cube (the "camera to the ceiling"
  // bug). AimAt returns the commanded (pre-drift) angle.
  QAngle view{0, 0, 0};
  if (mark > 0) {
    portal2_harness::MacroResult aim = AimAt(mark);
    if (!aim.ok()) return aim;  // BAD_MARK / NO_PLAYER / CANCELLED bubble up
    view = QAngle{aim.aim_pitch(), aim.aim_yaw(), 0};
  } else {
    auto v = std::make_shared<QAngle>();
    bool ran = RunOnMainThreadSync(context_, [v]() {
      QAngle cur = engine->GetAngles(Slot());
      *v = ApplyAbsoluteView(QAngle{kReleasePitch, cur.y, 0});
    });
    if (!ran) {
      portal2_harness::MacroResult r;
      r.set_ok(false);
      r.set_result_code("CANCELLED");
      return r;
    }
    view = *v;
  }

  // +use pulse drops the carried object, holding `view` so it lands where we
  // aimed. The caller tracks held-state, so there's nothing to confirm --
  // always SUCCESS. Caveat: +use is a context toggle, so a release issued while
  // NOT holding (but stood on a grabbable) GRABS instead; the caller's
  // held-tracking keeps intent aligned.
  PulseUse(kSettle, view);
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
  PulseUse(kSettle, QAngle{aim.aim_pitch(), aim.aim_yaw(), 0});

  portal2_harness::MacroResult r;
  r.set_ok(true);
  r.set_result_code("SUCCESS");
  r.set_detail("interacted with mark " + std::to_string(mark));
  return r;
}

// Debug: A* a route to a mark and print it over the occupancy grid (. walkable,
// # blocked/obstacle, o route, @ player, * goal). The route should bend around
// cubes/walls. Does NOT move the player -- a read-only verify gate. The grid
// is WORLD-absolute (+y/north at top), not view-relative.
CON_COMMAND(sar_harness_goto_plan,
            "sar_harness_goto_plan <mark> [radius] - A* a route to a mark and "
            "print it over the grid (does not move the player). go_to "
            "planner.\n") {
  if (args.ArgC() < 2) {
    console->Print("usage: sar_harness_goto_plan <mark> [radius]\n");
    return;
  }
  ServerEnt* pl = server ? server->GetPlayer(1) : nullptr;
  if (!pl) {
    console->Print("goto_plan: no player.\n");
    return;
  }
  int mark = std::atoi(args[1]);
  int radius = args.ArgC() > 2 ? std::atoi(args[2]) : 12;
  if (radius < 1) radius = 1;
  if (radius > 40) radius = 40;

  Vector center;
  std::string code;
  if (!ResolveMarkCenter(mark, &center, &code)) {
    console->Print("goto_plan: mark %d -> %s\n", mark, code.c_str());
    return;
  }
  Vector feet = pl->abs_origin();
  auto [idx, ser] = markTable.GetEntityFromMark(mark);
  uint32_t targetKey =
      idx >= 0 ? PackEntKey(idx, static_cast<uint16_t>(ser)) : 0;
  ICollideable& coll = pl->collision();
  GoToPlanner planner(coll.OBBMins(), coll.OBBMaxs(), feet.z, targetKey,
                      g_heldEntityKey.load());
  std::vector<Vector> path = planner.Plan(feet, center);

  console->Print("goto_plan mark=%d: %zu waypoints to (%.0f,%.0f)\n", mark,
                 path.size(), center.x, center.y);
  if (path.empty()) {
    console->Print("  no route (blocked, off-grid, or cap hit).\n");
    return;
  }
  std::unordered_set<uint32_t> onPath;
  for (const Vector& w : path)
    onPath.insert(
        GoToPlanner::CellKey(GoToPlanner::CellX(w.x), GoToPlanner::CellY(w.y)));
  int pcx = GoToPlanner::CellX(feet.x), pcy = GoToPlanner::CellY(feet.y);
  int gcx = GoToPlanner::CellX(center.x), gcy = GoToPlanner::CellY(center.y);
  for (int dy = radius; dy >= -radius; --dy) {  // +y (north) at top
    std::string row;
    for (int dx = -radius; dx <= radius; ++dx) {
      int cx = pcx + dx, cy = pcy + dy;
      if (cx == pcx && cy == pcy)
        row += '@';
      else if (cx == gcx && cy == gcy)
        row += '*';
      else if (onPath.count(GoToPlanner::CellKey(cx, cy)))
        row += 'o';
      else
        row += planner.At(cx, cy).state == GoToPlanner::WALKABLE ? '.' : '#';
    }
    console->Print("%s\n", row.c_str());
  }
}

// Debug: print where `release` would place the held cube to seat it on a button
// mark -- the press surface it found, the cube-centre target, the teleport
// origin, and the preserved angles. Does NOT move anything. Pass a cube mark to
// test a specific cube instead of whatever is held (so it works without a
// grab).
CON_COMMAND(sar_harness_seat_check,
            "sar_harness_seat_check <button-mark> [cube-mark] - print the seat "
            "release would place the held (or given) cube on, over a button. "
            "Does not move anything.\n") {
  if (args.ArgC() < 2) {
    console->Print("usage: sar_harness_seat_check <button-mark> [cube-mark]\n");
    return;
  }
  ServerEnt* pl = server ? server->GetPlayer(1) : nullptr;
  if (!pl) {
    console->Print("seat_check: no player.\n");
    return;
  }
  std::string code;
  int buttonMark = std::atoi(args[1]);
  CEntInfo* binfo = nullptr;
  if (!ResolveMarkInfo(buttonMark, &binfo, &code)) {
    console->Print("seat_check: button mark %d -> %s\n", buttonMark,
                   code.c_str());
    return;
  }
  ServerEnt* cube = nullptr;
  if (args.ArgC() > 2) {
    int cubeMark = std::atoi(args[2]);
    CEntInfo* cinfo = nullptr;
    if (!ResolveMarkInfo(cubeMark, &cinfo, &code)) {
      console->Print("seat_check: cube mark %d -> %s\n", cubeMark,
                     code.c_str());
      return;
    }
    cube = SE(cinfo->m_pEntity);
  } else if (!(cube = HeldCube())) {
    console->Print(
        "seat_check: not holding a cube (pass a cube mark to test)\n");
    return;
  }

  const char* bcls = server->GetEntityClassName(binfo->m_pEntity);
  Seat s = ComputeSeat(SE(binfo->m_pEntity), cube);
  if (!s.ok) {
    console->Print("seat_check: no press surface under %s mark %d\n", bcls,
                   buttonMark);
    return;
  }
  console->Print("seat_check button=%d (%s)\n", buttonMark, bcls);
  console->Print("  surface  z=%.2f  normal.z=%.3f\n", s.surface.z, s.normalZ);
  console->Print("  center   (%.1f, %.1f, %.1f)\n", s.center.x, s.center.y,
                 s.center.z);
  console->Print("  origin   (%.1f, %.1f, %.1f)\n", s.origin.x, s.origin.y,
                 s.origin.z);
  console->Print("  angles   (p%.1f y%.1f r%.1f)\n", s.angles.x, s.angles.y,
                 s.angles.z);

  // Fairness battery: would a clean hand-drop from here have reached this seat?
  Fairness f = CheckFairness(SE(binfo->m_pEntity), cube, pl, s);
  console->Print("  fair: %s\n", f.fair ? "YES" : "NO");
  console->Print("    reach        %s  (d=%.1f <= %.0f)\n", f.reach ? "Y" : "N",
                 f.reachDist, kGrabRange);
  console->Print("    press-normal %s  (nz=%.3f > %.2f)\n",
                 f.pressNormal ? "Y" : "N", s.normalZ, kPressNormalMin);
  console->Print("    corridor     %s  (gap=%.1f)\n", f.corridor ? "Y" : "N",
                 f.corridorGap);
  console->Print("    eye-line     %s%s%s\n", f.eyeLine ? "Y" : "N",
                 f.eyeLine ? "" : "  blocked by ", f.eyeHit.c_str());
  if (f.seatClear)
    console->Print("    seat         CLEAR\n");
  else if (f.selfOnSeat)
    console->Print("    seat         SELF (player on seat -> displace)\n");
  else
    console->Print("    seat         OCCUPIED by %s\n", f.occupant.c_str());
  if (f.selfOnSeat) {
    Vector standoff;
    if (FindPlayerStandoff(SE(binfo->m_pEntity), pl, &standoff))
      console->Print("    standoff     (%.1f, %.1f, %.1f)\n", standoff.x,
                     standoff.y, standoff.z);
    else
      console->Print("    standoff     none found\n");
  }
}
