#include "MacroExecutor.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#include "Command.hpp"
#include "Entity.hpp"
#include "Event.hpp"
#include "Features/EntityList.hpp"
#include "Features/Tas/TasController.hpp"
#include "Features/Tas/TasPlayer.hpp"
#include "Features/Tas/TasScript.hpp"
#include "Harness.hpp"
#include "HarnessThread.hpp"
#include "LaserGeometry.hpp"
#include "MarkTable.hpp"
#include "Modules/Console.hpp"
#include "Modules/Engine.hpp"
#include "Modules/Server.hpp"
#include "NavSkeleton.hpp"
#include "Offsets.hpp"
#include "PortalRead.hpp"
#include "Scheduler.hpp"
#include "SurfaceMarkTable.hpp"
#include "Utils.hpp"
#include "Utils/Math.hpp"
#include "Utils/SDK/EntityEdict.hpp"
#include "Utils/SDK/Trace.hpp"
#include "Variable.hpp"
#include "verbs/GoTo.hpp"

Variable sar_harness_goto_debug(
    "sar_harness_goto_debug", "0",
    "Log per-batch go_to flood-follow steering (waypoint, heading, dist).\n");

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
constexpr float kGoToReach = 32.0f;  // go_to stops this near the target's stand
                                     // cell -- tighter than kReachRadius so a
                                     // follow-up pick_up/interact is in range
// pass_through: approach the mouth, then push straight in until the engine
// transits the body (feet jump to the far side).
constexpr float kMouthStandoff = 28.0f;  // approach point in front of the mouth
constexpr float kMouthReach = 80.0f;  // march must leave us at least this near
constexpr int kPassBatch = 2;         // ticks per push-into-mouth batch
constexpr int kPassMaxTicks = 160;    // give up pushing into the mouth
constexpr float kPassJump = 48.0f;    // feet displacement that flags a transit
constexpr float kEmergeRadius = 128.0f;  // emerged this near the partner = ok
// jump_into fling (from the P0 fling recon): jump = a +207 impulse, the funnel
// centers us over a floor portal given a held down-aim, transit = a huge
// single-tick position jump (observed 700-880u; 128 clears any real movement).
constexpr int kJumpHold = 2;        // ticks to hold key_jump (edge-triggered)
constexpr int kJumpMaxTicks = 200;  // give up waiting for the funnel transit
constexpr float kJumpTransit =
    128.0f;  // single-tick pos jump that flags transit
constexpr float kFloorNormalZ =
    0.7f;  // portal normal.z gate: floor-ish, <45deg
constexpr float kJumpApproach =
    40.0f;                          // approach stops this far short of center
constexpr int kJumpRunTicks = 120;  // give up approaching the mouth
constexpr float kJumpMoveSpeed =
    0.5f;  // gentle forward: arc lands at the mouth
           // with little horizontal for the funnel to
           // zero (a full run overshoots)
constexpr float kDropStandoff =
    64.0f;  // drop_into object arm: carry-stop this far in front of the mouth
            // so the straight march never crosses the disc (the object drops
            // over it)
constexpr int kMaxReplans = 2;  // re-Build+re-Plan attempts on a stalled follow
constexpr float kFollowAdvance =
    24.0f;  // advance to the next plan waypoint within this 2D distance
constexpr float kFollowStallEps =
    2.0f;  // per-batch feet move under this counts as no progress
constexpr int kFollowStallBatches =
    8;  // consecutive no-progress batches -> re-plan (then STUCK)
constexpr float kStuckEps = 1.0f;      // <this much progress/iter twice = stuck
constexpr float kProbeHeight = 18.0f;  // lift the edge ray to ~step height
constexpr float kStepAhead = 24.0f;    // edge ray is cast this far ahead
constexpr float kStepDownMax = 64.0f;  // no floor within this below = edge

// Interaction-verb tuning (pick_up / release / interact).
constexpr int kSettle = 20;       // ticks to let a grab/drop/use resolve
constexpr int kUseHoldTicks = 3;  // hold +use past the press/tick-advance race
constexpr float kGrabRange = 96.0f;     // reach gate: ~80u radius + half a cube
constexpr float kReleasePitch = 75.0f;  // mark-less release: look-down pitch
constexpr int kReleaseDropSettle = 4;   // ticks to free the grab before a seat
constexpr int kDropTries =
    3;  // +use drop pulses to retry if the hand stays full
constexpr int kSeatSettle = 32;    // ticks for the press to register post-seat
constexpr int kSeatDwellGap = 16;  // ticks between the two press reads
constexpr int kButtonRiseTicks = 12;  // ticks for a button to rise once the
                                      // player steps off it

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
constexpr float kBeamClearSin =
    0.4f;  // reject interpose standoffs within ~24 deg of the beam axis (the
           // player would occlude the beam it's clearing the cube onto)
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

// Eye position of the host player (slot 0 -> entity index 1): abs_origin +
// view offset + portal-local eye offset. False if no player. Main thread only.
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

// Button classes that accept an object placed on their press surface. The
// pedestal prop_button is a use-press, not a place, so it is not here.
bool IsButtonClass(const char* cls) {
  if (!cls) return false;
  std::string s(cls);
  return s == "prop_floor_button" || s == "prop_floor_cube_button" ||
         s == "prop_floor_ball_button" || s == "prop_under_floor_button";
}

// OBB centre of an entity, unrotated (origin + (mins+maxs)/2): correct for
// axis-aligned elements, approximate for tilted ones. Main thread only (live
// entity).
Vector EntityCenter(ServerEnt* se) {
  ICollideable& coll = se->collision();
  return se->abs_origin() + (coll.OBBMins() + coll.OBBMaxs()) * 0.5f;
}

// Packed key (index<<16 | serial) -> live ServerEnt, serial-revalidated so a
// recycled slot can't resolve to a stranger. null if absent or stale. Main
// thread only.
ServerEnt* EntFromKey(uint32_t key) {
  if (!key) return nullptr;
  int index = static_cast<int>(key >> 16);
  uint16_t serial = static_cast<uint16_t>(key & 0xFFFF);
  CEntInfo* info = entityList->GetEntityInfoByIndex(index);
  if (!info || !info->m_pEntity ||
      static_cast<uint16_t>(info->m_SerialNumber) != serial)
    return nullptr;
  return SE(info->m_pEntity);
}

// The cube the last pick_up grabbed (0 = none). Main thread only.
ServerEnt* HeldCube() { return EntFromKey(g_heldEntityKey.load()); }

// Where a held cube should land to rest on a button's press surface. A press is
// pure bbox-overlap of the button's trigger, so the recipe is geometric: trace
// down through the button centre to read the real collision plane, sit the cube
// on it (bottom a hair into the trigger), laid flat (pitch/roll zeroed, yaw
// kept) so it settles stably instead of toppling off-centre. Read-only --
// traces + field reads, mutates nothing. Main thread only (live entities).
struct Seat {
  bool ok;  // false => no solid press surface found under the button
  Vector
      origin;  // target m_vecAbsOrigin (CBaseEntity::Teleport takes an origin)
  QAngle angles;   // flat placement angle (pitch/roll zeroed, cube yaw kept)
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
  // collision-to-world matrix so a tilted cube stays consistent.
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
  s.angles = QAngle{0, cube->abs_angles().y, 0};  // lay flat, keep yaw
  s.ok = true;
  return s;
}

// Horizontal half-extent of an entity's OBB -- the radius the player must clear
// to avoid shoving it (a button to step off, a cube about to be seated).
float HalfWidthXY(ServerEnt* e) {
  ICollideable& c = e->collision();
  Vector mn = c.OBBMins(), mx = c.OBBMaxs();
  return 0.5f * Vector{mx.x - mn.x, mx.y - mn.y, 0}.Length2D();
}

// A spot ringR off `ringCenter` where the player hull fits on solid floor,
// taken from the first fit in a ring of bearings (scanned from startYaw). Steps
// the player clear of a seat -- a button it stands on, or a laser seat it would
// otherwise shove the cube off. footprintHalfW sizes the ring to whatever the
// player must clear; startYaw aims the scan (e.g. perpendicular to a beam, so
// the displaced player doesn't occlude it). Read-only -- traces. Main thread.
bool FindPlayerStandoff(const Vector& ringCenter, float footprintHalfW,
                        ServerEnt* player, Vector* out, float startYaw = 0.0f,
                        const float* beamYaw = nullptr) {
  ICollideable& pcoll = player->collision();
  Vector pmin = pcoll.OBBMins(), pmax = pcoll.OBBMaxs();
  float ringR = footprintHalfW + HalfWidthXY(player) + kStandoffMargin;

  float feetZ = player->abs_origin().z;
  CTraceFilterSimple filter;
  filter.SetPassEntity(player);
  for (int i = 0; i < kStandoffBearings; ++i) {
    float bearing = startYaw + (360.0f / kStandoffBearings) * i;
    // A laser interpose passes the beam yaw: skip bearings near the beam axis,
    // where the standoff would sit on the beam and occlude it (the "player
    // blocks the beam" miss). Both axis directions are rejected.
    if (beamYaw &&
        std::fabs(std::sin(DEG2RAD(bearing - *beamYaw))) < kBeamClearSin)
      continue;
    QAngle a{0, bearing, 0};
    Vector dir;
    Math::AngleVectors(a, &dir);
    float cx = ringCenter.x + dir.x * ringR, cy = ringCenter.y + dir.y * ringR;
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

// The first failing fairness gate, for a result detail. "" if all pass.
std::string FairnessFailReason(const Fairness& f) {
  if (!f.reach) return "out-of-reach";
  if (!f.pressNormal) return "not-flat";
  if (!f.corridor) return "blocked-corridor";
  if (!f.eyeLine) return "blocked-sightline";
  if (!f.seatClear && !f.selfOnSeat) return "seat-occupied";
  return "";
}

// Teleport a free entity to an absolute origin + angles, zeroing its velocity.
// One game-layer vfunc moves it, relinks it spatially, and resyncs vphysics.
// The entity must already be free (drop a held cube first), or the grab
// controller fights the move on the next tick. Main thread only.
void SeatEntity(void* ent, const Vector& origin, const QAngle& angles) {
  Vector zeroVel{0, 0, 0};
  using _Teleport =
      void(__rescall*)(void* ent, const Vector* pos, const QAngle* ang,
                       const Vector* vel, bool slowAccurate);
  _Teleport Teleport = Memory::VMT<_Teleport>(ent, Offsets::StartTouch + 11);
  Teleport(ent, &origin, &angles, &zeroVel, true);
  // Teleport zeros LINEAR velocity only; a held-then-dropped cube keeps its
  // spin and walks off the seat a tick later. Zero the physics object's
  // velocity (linear + angular) so it settles in place.
  IPhysicsObject* phys = SE(ent)->collision().GetVPhysicsObject();
  if (phys) {
    Vector zero{0, 0, 0};
    using _SetVelocity = void(__rescall*)(void*, const Vector*, const Vector*);
    _SetVelocity SetVelocity =
        Memory::VMT<_SetVelocity>(phys, Offsets::SetVelocity);
    SetVelocity(phys, &zero, &zero);
  }
}

// Mark -> world-space aim point (OBB centre). Sets *code on a bad mark. Main
// thread only.
bool ResolveMarkCenter(int mark, Vector* outCenter, std::string* code) {
  CEntInfo* info = nullptr;
  if (!ResolveMarkInfo(mark, &info, code)) return false;
  *outCenter = EntityCenter(SE(info->m_pEntity));
  return true;
}

bool AllDigits(const char* s) {
  if (!*s) return false;
  for (; *s; ++s)
    if (!std::isdigit(static_cast<unsigned char>(*s))) return false;
  return true;
}

// A target label from the percept's Set-of-Marks namespace: "<n>" entity mark,
// "S<n>" surface panel, "Pb"/"Po" portal. NONE for an empty/garbage label.
struct TargetRef {
  enum Kind { NONE, ENTITY, PANEL, PORTAL } kind = NONE;
  int mark = 0;         // ENTITY: entity mark; PANEL: surface mark
  bool orange = false;  // PORTAL: orange vs blue
  float u = 0.5f,
        v = 0.5f;  // PANEL: fractional target point, (0.5,0.5) = center
};

// "Pb"/"Po", "Sn", or "Sn@u,v" (fractional panel point, u,v in [0,1]), or a
// bare entity mark. A malformed "@u,v" (no comma) rejects to NONE.
TargetRef ClassifyTarget(const std::string& t) {
  TargetRef r;
  if (t == "Pb" || t == "Po") {
    r.kind = TargetRef::PORTAL;
    r.orange = (t == "Po");
  } else if (t.size() >= 2 && t[0] == 'S') {
    size_t at = t.find('@');
    std::string digits =
        t.substr(1, at == std::string::npos ? std::string::npos : at - 1);
    if (AllDigits(digits.c_str())) {
      r.kind = TargetRef::PANEL;
      r.mark = atoi(digits.c_str());
      if (at != std::string::npos) {
        size_t comma = t.find(',', at + 1);
        if (comma == std::string::npos) {
          r.kind = TargetRef::NONE;
          return r;
        }
        auto clamp01 = [](float x) {
          return std::min(std::max(x, 0.0f), 1.0f);
        };
        r.u = clamp01((float)atof(t.substr(at + 1, comma - at - 1).c_str()));
        r.v = clamp01((float)atof(t.substr(comma + 1).c_str()));
      }
    }
  } else if (AllDigits(t.c_str())) {
    r.kind = TargetRef::ENTITY;
    r.mark = atoi(t.c_str());
  }
  return r;
}

// A target label -> world center. Sets *code = "BAD_MARK" on an absent/garbage
// target. Main thread only (touches the entity list / portal read).
bool ResolveTarget(const std::string& target, Vector* outCenter,
                   std::string* code) {
  TargetRef ref = ClassifyTarget(target);
  if (ref.kind == TargetRef::PORTAL) {
    LivePortal lp = ReadPortal(ref.orange);
    if (!lp.active) {
      *code = "BAD_MARK";
      return false;
    }
    *outCenter = lp.center;
    return true;
  }
  if (ref.kind == TargetRef::PANEL) {
    PanelDesc panel;
    if (!surfaceMarkTable.GetPanelFromMark(ref.mark, &panel)) {
      *code = "BAD_MARK";
      return false;
    }
    *outCenter = ResolvePanelPoint(panel, ref.u, ref.v);
    return true;
  }
  if (ref.kind == TargetRef::ENTITY)
    return ResolveMarkCenter(ref.mark, outCenter, code);
  *code = "BAD_MARK";
  return false;
}

// The entity mark for a verb that needs an entity target. False with *code set
// on a panel/portal target ("WRONG_TARGET") or an empty/garbage one
// ("BAD_MARK"). Pure string parse -- no entity list, callable off any thread.
bool RequireEntityMark(const std::string& target, int* outMark,
                       std::string* code) {
  TargetRef ref = ClassifyTarget(target);
  if (ref.kind == TargetRef::ENTITY) {
    *outMark = ref.mark;
    return true;
  }
  *code = ref.kind == TargetRef::NONE ? "BAD_MARK" : "WRONG_TARGET";
  return false;
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

// The aiming primitive: clamp pitch, zero roll, set the absolute view, and zero
// the framebulk view delta so the TAS per-tick re-apply (GetAngles() -
// viewAnalog) preserves it (delta 0 => idempotent). Returns the commanded angle
// (== camera if SetAngles survived). Main thread only.
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
// +use is edge-triggered (acts on the IN_USE 0->1 tick), so hold it
// kUseHoldTicks rather than 1: a 1-tick press can be cleared before the
// simulated tick reads it. Extra held ticks add no new edge, so it stays ONE
// logical press (can't grab-then-drop). The view is re-asserted every settle
// batch because a single SetAngles drifts -- the engine ratchets pitch to the
// ceiling each tick -- which would swing the held object up and drop it wrong.
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

// Drop the held object via +use, confirming the hand is actually empty
// afterwards (m_hAttachedObject clears) and re-pulsing if not -- the +use drop
// edge intermittently misses on a single pulse. False if still holding after
// the retries, or the stream dropped. gRPC thread only (advances ticks).
bool DropHeld(grpc::ServerContext* context, QAngle view, int settle) {
  for (int attempt = 0; attempt < kDropTries; ++attempt) {
    PulseUse(settle, view);
    auto holding = std::make_shared<bool>(true);
    if (!RunOnMainThreadSync(context, [holding]() {
          ServerEnt* pl = server->GetPlayer(1);
          if (pl) *holding = (bool)pl->field<CBaseHandle>("m_hAttachedObject");
        }))
      return false;  // cancelled
    if (!*holding) return true;
  }
  return false;
}

// Free the +use grab for a teleport-seat (where the drop spot is thrown away --
// the cube gets teleported next). A seat-aimed steep-down view drives the held
// cube into the floor/button and +use then refuses to release it; sweep a few
// clear-air look directions (up first -- open over a player on a floor button;
// then behind -- we just marched from there) until the hand empties. False if
// none freed it, or the stream dropped. gRPC thread only.
bool FreeGrab(grpc::ServerContext* context, int settle) {
  static const QAngle kFreeViews[] = {
      {-55, 0, 0}, {-80, 0, 0}, {0, 180, 0}, {0, 0, 0}};
  for (const QAngle& fv : kFreeViews) {
    auto view = std::make_shared<QAngle>();
    if (!RunOnMainThreadSync(context, [view, fv]() {
          QAngle cur = engine->GetAngles(Slot());
          *view = ApplyAbsoluteView(QAngle{fv.x, cur.y + fv.y, 0});
        }))
      return false;  // cancelled
    if (DropHeld(context, *view, settle)) return true;
  }
  return false;
}

// Turn the camera to face an entity -- the just-released/interposed cube -- so
// its outcome stays visible after the player is teleported off the seat. Best
// effort: a vanished entity or no-player leaves the view as-is. One settle tick
// so the commanded view rides back on the next Observe. Main thread.
void LookBackAt(grpc::ServerContext* context, uint32_t entKey) {
  if (!RunOnMainThreadSync(context, [entKey]() {
        Vector eye;
        ServerEnt* ent = EntFromKey(entKey);
        if (ent && PlayerEye(&eye))
          ApplyAbsoluteView(AimAnglesTo(eye, EntityCenter(ent)));
      }))
    return;
  AdvanceTicksBlocking(1);
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

// A NavSkeleton projection reason as a word, for a go_to detail string.
const char* NavReasonWord(uint8_t reason) {
  switch (reason) {
    case NavSkeleton::SEVERED:
      return "severed";
    case NavSkeleton::ABOVE_REACH:
      return "above reach";
    case NavSkeleton::BELOW_REACH:
      return "below reach";
    case NavSkeleton::NO_FLOOR:
      return "no floor";
    default:
      return "none";
  }
}

// The result of one FollowTo: where the body ended up and why it stopped.
// The codes are the NavSkeleton plan codes plus NO_PLAYER; a plan that reaches
// the real goal is SUCCESS+reached, the nearest-reachable projection is
// REACHED_PROJECTION, a stalled follow that never arrives is STUCK.
struct FollowOutcome {
  std::string code = "NO_ROUTE";  // SUCCESS/REACHED_PROJECTION/NO_ROUTE/STUCK/
                                  // NO_PLAYER
  bool reached = false;           // arrived AND the plan reached the real goal
  bool cancelled = false;     // stream dropped; caller skips its own cleanup
  float dist = 0;             // final 2D distance to the goal target
  Vector finalFeet{0, 0, 0};  // for moved-distance telemetry
  uint8_t blockReason = NavSkeleton::NONE;  // why a projection, for the detail
};

// Walk the body to `dest` over the NavSkeleton flood. Build + Plan a cell route
// from the live feet, then follow its waypoints in a straight line -- no local
// avoidance, because the flood already carved a path around every solid (props
// included). A leg that stops making progress re-Builds and re-Plans, up to
// kMaxReplans times, before giving up as STUCK. Arrival is a 3D test against
// the plan's standPos (the goal cell, or the nearest reachable projection when
// the goal is off the graph). Owns the terminal stop: clears input, zeroes walk
// velocity, settles. gRPC thread only (it advances ticks).
FollowOutcome FollowTo(grpc::ServerContext* context, const Vector& dest,
                       float reachRadius, int tickBudget) {
  FollowOutcome out;
  int budget = tickBudget;  // ticks shared across re-plans

  for (int attempt = 0; attempt <= kMaxReplans; ++attempt) {
    // Flood + plan at the live pose: movers/props are wherever they are now.
    auto plan = std::make_shared<NavSkeleton::PlanResult>();
    auto startFeet = std::make_shared<Vector>();
    auto havePlayer = std::make_shared<bool>(false);
    bool ran =
        RunOnMainThreadSync(context, [plan, startFeet, havePlayer, dest]() {
          ServerEnt* pl = server->GetPlayer(1);
          if (!pl) return;
          *havePlayer = true;
          *startFeet = pl->abs_origin();
          NavSkeleton nav;
          nav.Build(engine->GetCurrentMapName());
          *plan = nav.Plan(*startFeet, dest);
        });
    if (!ran) {
      out.cancelled = true;
      return out;
    }
    if (!*havePlayer) {
      out.code = "NO_PLAYER";
      break;
    }
    if (attempt == 0) out.finalFeet = *startFeet;  // honest moved=0 on a bail
    out.blockReason = plan->blockReason;
    if (plan->code == NavSkeleton::NO_ROUTE) {
      out.code = "NO_ROUTE";
      break;
    }

    Vector stand = plan->standPos;
    size_t wp = 0;  // next unreached waypoint in plan->steps
    int stallBatches = 0;
    Vector prevFeet{0, 0, 0};
    bool havePrev = false;
    bool arrived = false, noPlayer = false, stalled = false;

    for (; budget > 0; budget -= kGoToTickBatch) {
      struct Step {
        bool noPlayer = false;
        bool arrived = false;
        bool stuck = false;
        size_t wp = 0;
        Vector feet{0, 0, 0};
      };
      auto step = std::make_shared<Step>();
      step->wp = wp;
      bool ok = RunOnMainThreadSync(
          context, [step, plan, stand, dest, reachRadius, prevFeet, havePrev,
                    budget, tickBudget]() {
            ServerEnt* pl = server->GetPlayer(1);
            if (!pl) {
              step->noPlayer = true;
              return;
            }
            Vector feet = pl->abs_origin();
            step->feet = feet;
            if (Vector{feet.x - stand.x, feet.y - stand.y, feet.z - stand.z}
                    .Length() <= reachRadius) {
              step->arrived = true;
              return;
            }
            // No-progress detector: the flood proved this walkable, so feet
            // that won't move mean we're wedged on something dynamic ->
            // re-plan.
            if (havePrev &&
                Vector{feet.x - prevFeet.x, feet.y - prevFeet.y, 0}.Length2D() <
                    kFollowStallEps)
              step->stuck = true;
            // Advance past any waypoint we're already on top of, then steer at
            // the next one (or straight at standPos once past the last).
            size_t w = step->wp;
            const auto& steps = plan->steps;
            while (w < steps.size() &&
                   Vector{steps[w].pos.x - feet.x, steps[w].pos.y - feet.y, 0}
                           .Length2D() <= kFollowAdvance)
              w++;
            step->wp = w;
            Vector aim = (w < steps.size()) ? steps[w].pos : stand;
            Vector fwd{aim.x - feet.x, aim.y - feet.y, 0};
            Vector up{0, 0, 1};
            QAngle a{0, 0, 0};
            Math::VectorAngles(fwd, up, &a);
            ApplyAbsoluteView(QAngle{0, a.y, 0});  // clears the framebulk...
            SetMoveFramebulk(0, 1);                // ...so drive AFTER it
            if (sar_harness_goto_debug.GetBool())
              console->Print(
                  "follow t=%d wp=%zu/%zu hdg=%+.0f dist=%.0f\n",
                  tickBudget - budget, w, steps.size(), a.y,
                  Vector{dest.x - feet.x, dest.y - feet.y, 0}.Length2D());
          });
      if (!ok) {
        out.cancelled = true;
        return out;
      }
      if (step->noPlayer) {
        noPlayer = true;
        break;
      }
      if (step->arrived) {
        arrived = true;
        break;
      }
      wp = step->wp;
      if (step->stuck) {
        if (++stallBatches >= kFollowStallBatches) {
          stalled = true;
          break;
        }
      } else {
        stallBatches = 0;
      }
      prevFeet = step->feet;
      havePrev = true;
      AdvanceTicksBlocking(kGoToTickBatch);
    }

    if (noPlayer) {
      out.code = "NO_PLAYER";
      break;
    }
    if (arrived) {
      out.reached = (plan->code == NavSkeleton::SUCCESS);
      out.code = out.reached ? "SUCCESS" : "REACHED_PROJECTION";
      break;
    }
    if (budget <= 0) {  // spent the whole budget without arriving
      out.code = "STUCK";
      break;
    }
    // stalled -> the loop re-plans; the last attempt falls through to STUCK.
    if (stalled && attempt == kMaxReplans) out.code = "STUCK";
  }

  // Terminal stop (unconditional): kill input + walk velocity, then settle, so
  // no drive latches into the next verb and any inherited momentum (e.g. a
  // coast out of pass_through) is shed even on an immediate arrival.
  // Player-guarded; the framebulk clear still scrubs a latched drive with no
  // player.
  Scheduler::OnMainThread([]() {
    ClearFramebulk();
    ServerEnt* pl = server->GetPlayer(1);
    if (pl) pl->field<Vector>("m_vecVelocity") = {0, 0, 0};
  });
  AdvanceTicksBlocking(kGoToSettle);
  auto feet = std::make_shared<Vector>(out.finalFeet);
  auto dist = std::make_shared<float>(out.dist);
  bool finRan = RunOnMainThreadSync(context, [feet, dist, dest]() {
    ServerEnt* pl = server->GetPlayer(1);
    if (!pl) return;
    *feet = pl->abs_origin();
    *dist = Vector{dest.x - feet->x, dest.y - feet->y, 0}.Length2D();
  });
  if (!finRan) {
    out.cancelled = true;
    return out;
  }
  out.dist = *dist;
  out.finalFeet = *feet;
  return out;
}

// interpose seat+reachability gates on already-resolved entities (shared by the
// verb and its dryrun recon). Fills *seat/*beamLen and returns "SEAT_OK", else
// the first failing reject code. Main thread only (traces + the planner).
std::string InterposeGate(void* emitter, void* cube, float percent,
                          Vector* seat, float* beamLen) {
  if (percent < 0.0f) percent = 0.0f;
  if (percent > 1.0f) percent = 1.0f;
  const char* cls = server->GetEntityClassName(emitter);
  if (!cls || std::strcmp(cls, "env_portal_laser")) return "NOT_EMITTER";
  if (!SE(emitter)->field<bool>("m_bLaserOn")) return "NO_BEAM";

  ServerEnt* pl = server->GetPlayer(1);
  if (!pl) return "NO_PLAYER";

  // Skip the player + held cube so a player standing in the beam doesn't
  // shorten it and pull the seat back onto the emitter housing.
  Vector E, F, hit;
  float len;
  if (!ComputeBeamSegment(emitter, &E, &F, &hit, &len, pl, cube) || len <= 0.0f)
    return "NO_BEAM";
  *beamLen = len;
  Vector P = E + F * (percent * len);

  if (!DownTraceRest(P, cube, seat)) return "NO_FLOOR";
  // A seat in slime/goo wrongly passes NO_FLOOR -- the down-trace lands on the
  // goo-bottom brush. Needs a point-contents read to catch.

  // Reachability is the carry's job: interpose follows the flood route to the
  // seat (same backend as go_to) and reports BLOCKED if it can't get there.
  return "SEAT_OK";
}

// True if the seated cube actually catches the beam: re-trace and check the
// beam now terminates within the cube's hull (the authoritative interception
// test, not pre-placement math). Main thread only.
bool ConfirmInterception(void* emitter, void* cube) {
  constexpr float kInterceptSlack =
      8.0f;  // beam end this close to cube = a hit
  Vector E, F, hit;
  float len;
  ComputeBeamSegment(emitter, &E, &F, &hit, &len);
  ICollideable& cc = SE(cube)->collision();
  Vector cmin = cc.OBBMins(), cmax = cc.OBBMaxs();
  float cubeR =
      0.5f * Vector{cmax.x - cmin.x, cmax.y - cmin.y, cmax.z - cmin.z}.Length();
  // Against the cube's ACTUAL position, never a computed seat -- a cube that
  // drifted or never reached the beam must read a miss even when the seat sat
  // near the beam's far terminus.
  return (hit - SE(cube)->abs_origin()).Length() < cubeR + kInterceptSlack;
}

// True if `cube` is currently catching any live emitter's beam (the seated gate
// for redirect_to). Scans emitters, skipping the transient (0,0,0) re-emit
// segments. Main thread only.
bool CubeOnAnyBeam(void* cube) {
  for (int i = 0; i < Offsets::NUM_ENT_ENTRIES; ++i) {
    CEntInfo* info = entityList->GetEntityInfoByIndex(i);
    if (!info || !info->m_pEntity || info->m_pEntity == cube) continue;
    void* em = info->m_pEntity;
    const char* cls = server->GetEntityClassName(em);
    if (!cls || std::strcmp(cls, "env_portal_laser")) continue;
    Vector eo = SE(em)->abs_origin();
    if (eo.x == 0 && eo.y == 0 && eo.z == 0) continue;  // transient re-emit seg
    if (!SE(em)->field<bool>("m_bLaserOn")) continue;
    if (ConfirmInterception(em, cube)) return true;
  }
  return false;
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
  if (verb == "aim_at") return AimAt(req.target());
  if (verb == "look") return Look(req.yaw(), req.pitch());
  if (verb == "go_to") return verbs::GoTo({this, context_}, req.target());
  if (verb == "move") return Move(req.dir(), req.ticks());
  if (verb == "wait") return Wait(req.ticks());
  if (verb == "done") return Done();
  if (verb == "pick_up") return PickUp(req.target());
  if (verb == "release") return Release(req.target());
  if (verb == "interact") return Interact(req.target());
  if (verb == "interpose") return Interpose(req);
  if (verb == "redirect_to") return RedirectTo(req);
  if (verb == "place_portal") return PlacePortal(req);
  if (verb == "pass_through") return PassThrough(req);
  if (verb == "jump_into") return JumpInto(req.target());
  if (verb == "drop_into") return DropInto(req);

  // press (a pedestal-button alias of interact) is not wired yet.
  r.set_ok(false);
  r.set_result_code("NOT_IMPLEMENTED");
  r.set_detail("verb '" + verb + "' not implemented yet");
  return r;
}

bool MacroExecutor::AimCube(uint32_t cubeKey, const Vector& aimPoint,
                            float* residual) {
  *residual = 0;
  // Re-centre on the cube's settled position now (it already passed the on-beam
  // gate), so a tossed cube is pulled back to it -- not chased wherever it
  // drifted. Re-seat with +X aimed at the point, then settle so the beam
  // re-propagates. The seat is flat (yaw-only) and frozen -> deterministic.
  auto seat = std::make_shared<Vector>(Vector{0, 0, 0});
  if (!RunOnMainThreadSync(context_, [seat, cubeKey]() {
        ServerEnt* cube = EntFromKey(cubeKey);
        if (cube) *seat = cube->abs_origin();
      }))
    return false;
  if (!RunOnMainThreadSync(context_, [cubeKey, aimPoint, seat]() {
        ServerEnt* cube = EntFromKey(cubeKey);
        if (cube)
          SeatEntity((void*)cube, *seat, ComputeRedirectYaw(*seat, aimPoint));
      }))
    return false;
  AdvanceTicksBlocking(kSeatSettle);
  auto res = std::make_shared<float>(0.0f);
  if (!RunOnMainThreadSync(context_, [res, cubeKey, aimPoint]() {
        ServerEnt* cube = EntFromKey(cubeKey);
        if (!cube) return;
        // residual: the cube's +X (its redirect axis) vs the ideal direction.
        Vector fwd;
        Math::AngleVectors(cube->abs_angles(), &fwd);
        Vector ideal = aimPoint - cube->abs_origin();
        Math::VectorNormalize(ideal);
        float dot = fwd.x * ideal.x + fwd.y * ideal.y + fwd.z * ideal.z;
        dot = std::max(-1.0f, std::min(1.0f, dot));
        *res = std::acos(dot) * 57.2958f;
      }))
    return false;
  *residual = *res;
  return true;
}

portal2_harness::MacroResult MacroExecutor::Interpose(
    const portal2_harness::MacroRequest& req) {
  portal2_harness::MacroResult r;
  int emitterMark;
  std::string tcode;
  if (!RequireEntityMark(req.target(), &emitterMark, &tcode)) {
    r.set_ok(false);
    r.set_result_code(tcode);
    r.set_detail("interpose emitter must be a laser mark, got \"" +
                 req.target() + "\"");
    return r;
  }
  std::string aim = req.aim();  // optional redirect aim -- any position mark,
  bool hasAim = !aim.empty();   // resolved + validated in the gate below
  float percent = req.percent();

  auto cancelled = []() {
    portal2_harness::MacroResult c;
    c.set_ok(false);
    c.set_result_code("CANCELLED");
    return c;
  };

  // 1. Gate (main thread): resolve the emitter mark + held cube, compute the
  // seat, or bail with the first failing reject.
  struct Gate {
    std::string code = "BAD_MARK";
    Vector seat{0, 0, 0};
    Vector aimPoint{0, 0, 0};  // resolved `aim`, if given
    float beamLen = 0;
  };
  auto g = std::make_shared<Gate>();
  bool ran = RunOnMainThreadSync(context_, [g, emitterMark, percent, aim]() {
    ServerEnt* cube = HeldCube();
    if (!cube) {
      g->code = "NOT_HOLDING";
      return;
    }
    CEntInfo* eInfo = nullptr;
    if (!ResolveMarkInfo(emitterMark, &eInfo, &g->code)) return;
    if (!aim.empty() && !ResolveTarget(aim, &g->aimPoint, &g->code)) return;
    g->code = InterposeGate(eInfo->m_pEntity, (void*)cube, percent, &g->seat,
                            &g->beamLen);
  });
  if (!ran) return cancelled();
  if (g->code != "SEAT_OK") {
    r.set_ok(false);
    r.set_result_code(g->code);
    r.set_detail("interpose: " + g->code);
    return r;
  }

  // 2. Carry the held cube to the seat (flood follower). The held cube is not
  // skipped in the flood, so a cube blocking the only doorway can leave us
  // short
  // -- reported as a carry BLOCKED below.
  uint32_t heldKey = g_heldEntityKey.load();
  FollowOutcome m = FollowTo(context_, g->seat, kReachRadius, kGoToMaxTicks);
  if (m.cancelled) return cancelled();
  if (!m.reached) {
    r.set_ok(false);
    r.set_result_code("BLOCKED");
    r.set_detail(Utils::ssprintf(
        "interpose: carry blocked %.0fu short of the seat", m.dist));
    return r;
  }

  // 3. Free the +use grab (a held cube fights the teleport) via a clear-air
  // sweep -- a steep-down drop wedges the cube into the floor and +use won't
  // release it -- then snap it onto the beam (the drop spot is thrown away).
  g_heldEntityKey = 0;  // hands empty for the drop + seat
  if (!FreeGrab(context_, kReleaseDropSettle)) {
    r.set_ok(false);
    r.set_result_code("NOT_INTERCEPTING");
    r.set_detail("interpose: could not free the held cube to snap it");
    return r;
  }

  // Step the player off the seat (scan perpendicular to the beam so it can't
  // occlude E->P) -- else its hull shoves the cube off the beam during the
  // settle. Best effort: with no room to step, the confirm below reports the
  // miss.
  if (!RunOnMainThreadSync(context_, [g, heldKey, emitterMark]() {
        ServerEnt* cube = EntFromKey(heldKey);
        ServerEnt* player = server->GetPlayer(1);
        CEntInfo* eInfo = nullptr;
        std::string code;
        if (!cube || !player || !ResolveMarkInfo(emitterMark, &eInfo, &code))
          return;
        float beamYaw = SE(eInfo->m_pEntity)->abs_angles().y;
        Vector standoff;
        if (FindPlayerStandoff(g->seat, HalfWidthXY(cube), player, &standoff,
                               beamYaw + 90.0f, &beamYaw))
          SeatEntity(player, standoff, player->abs_angles());
      }))
    return cancelled();

  // 4. Re-seat at the known seat (with +X aimed at the point if an aim was
  // given), then confirm the hull intercepts the emitter's beam. Re-seating at
  // g->seat -- not the cube's drifted origin -- re-centres a cube that walked
  // off. The seat is flat (yaw-only) and frozen, so the pose is deterministic.
  // Whether the aim then powers a downstream target is percept state (the
  // observe returns each target's `powered`), so it is not re-confirmed here.
  bool ranSeat = RunOnMainThreadSync(context_, [g, heldKey, hasAim]() {
    ServerEnt* cube = EntFromKey(heldKey);
    if (!cube) return;
    QAngle yaw =
        hasAim ? ComputeRedirectYaw(g->seat, g->aimPoint) : QAngle{0, 0, 0};
    SeatEntity((void*)cube, g->seat, yaw);
  });
  if (!ranSeat) return cancelled();
  AdvanceTicksBlocking(kSeatSettle);
  auto intercept = std::make_shared<bool>(false);
  bool ranC =
      RunOnMainThreadSync(context_, [intercept, heldKey, emitterMark]() {
        ServerEnt* cube = EntFromKey(heldKey);
        CEntInfo* eInfo = nullptr;
        std::string code;
        if (!cube || !ResolveMarkInfo(emitterMark, &eInfo, &code)) return;
        *intercept = ConfirmInterception(eInfo->m_pEntity, (void*)cube);
      });
  if (!ranC) return cancelled();

  LookBackAt(context_, heldKey);  // face the cube so its beam state is visible

  if (!*intercept) {
    r.set_ok(false);
    r.set_result_code("NOT_INTERCEPTING");
    r.set_detail(
        Utils::ssprintf("interpose: not intercepting at (%.0f %.0f %.0f)",
                        g->seat.x, g->seat.y, g->seat.z));
    return r;
  }
  r.set_ok(true);
  r.set_result_code("ON_BEAM");
  r.set_detail(Utils::ssprintf("interpose: cube on beam at (%.0f %.0f %.0f)%s",
                               g->seat.x, g->seat.y, g->seat.z,
                               hasAim ? ", aimed" : ""));
  return r;
}

static const char* PortalRejectCode(int res) {
  switch (res) {
    case PORTAL_PLACEMENT_CANT_FIT:
      return "CANT_FIT";
    case PORTAL_PLACEMENT_CLEANSER:
      return "FIZZLED";
    case PORTAL_PLACEMENT_OVERLAP_LINKED:
    case PORTAL_PLACEMENT_OVERLAP_PARTNER_PORTAL:
      return "OVERLAP";
    default:  // INVALID_VOLUME / INVALID_SURFACE / PASSTHROUGH_SURFACE
      return "NOT_PORTALABLE";
  }
}

// A portal that hit the target panel sits on its plane, inside its tile AABB
// (edge slack covers the placement helper's nudge). A shot that grazed an
// occluder or an adjacent surface lands off it -> place_portal fails NO_LOS.
constexpr float kPanelPlaneTol = 16.0f;
constexpr float kPanelEdgeTol = 16.0f;
bool PortalOnPanel(const Vector& p, const PanelDesc& panel) {
  Vector d{p.x - panel.center.x, p.y - panel.center.y, p.z - panel.center.z};
  float offPlane =
      std::abs(d.x * panel.planeNormal.x + d.y * panel.planeNormal.y +
               d.z * panel.planeNormal.z);
  if (offPlane > kPanelPlaneTol) return false;
  return p.x >= panel.mins.x - kPanelEdgeTol &&
         p.x <= panel.maxs.x + kPanelEdgeTol &&
         p.y >= panel.mins.y - kPanelEdgeTol &&
         p.y <= panel.maxs.y + kPanelEdgeTol &&
         p.z >= panel.mins.z - kPanelEdgeTol &&
         p.z <= panel.maxs.z + kPanelEdgeTol;
}

portal2_harness::MacroResult MacroExecutor::PlacePortal(
    const portal2_harness::MacroRequest& req) {
  portal2_harness::MacroResult r;
  const std::string color = req.color();
  bool orange = (color == "orange");
  TargetRef ref = ClassifyTarget(req.target());
  if (ref.kind != TargetRef::PANEL) {
    r.set_ok(false);
    r.set_result_code(ref.kind == TargetRef::NONE ? "BAD_MARK"
                                                  : "WRONG_TARGET");
    r.set_detail("place_portal needs a wall panel Sn, got \"" + req.target() +
                 "\"");
    return r;
  }
  int surfaceMark = ref.mark;

  auto cancelled = []() {
    portal2_harness::MacroResult c;
    c.set_ok(false);
    c.set_result_code("CANCELLED");
    return c;
  };

  // 1. Aim the view at the panel center, so the shot comes from the player's
  // eye -- a portal you can't line a shot up on shouldn't be placeable.
  struct Aim {
    bool ok = false;
    std::string code = "BAD_MARK";
    QAngle view{0, 0, 0};
  };
  auto aim = std::make_shared<Aim>();
  bool aimRan = RunOnMainThreadSync(context_, [aim, surfaceMark, pu = ref.u,
                                               pv = ref.v]() {
    PanelDesc panel;
    if (!surfaceMarkTable.GetPanelFromMark(surfaceMark, &panel)) return;
    Vector eye;
    if (!PlayerEye(&eye)) {
      aim->code = "NO_PLAYER";
      return;
    }
    aim->view =
        ApplyAbsoluteView(AimAnglesTo(eye, ResolvePanelPoint(panel, pu, pv)));
    aim->ok = true;
  });
  if (!aimRan) return cancelled();
  if (!aim->ok) {
    r.set_ok(false);
    r.set_result_code(aim->code);
    r.set_detail("place_portal: " + aim->code);
    return r;
  }
  AdvanceTicksBlocking(1);  // camera rides to the panel + the view takes

  // 2. Gate (main thread): prime the gun, fire from the eye along the aimed
  // view, and commit only if the shot lands ON the target panel -- an occluded
  // or off-surface shot fails NO_LOS instead of magically placing.
  struct Gate {
    std::string code = "NO_LOS";
    unsigned char linkage = 0;
    Vector placed{0, 0, 0};
    bool usedHelper = false;
  };
  auto g = std::make_shared<Gate>();
  bool ran = RunOnMainThreadSync(context_, [g, surfaceMark, orange, aim]() {
    PanelDesc panel;
    if (!surfaceMarkTable.GetPanelFromMark(surfaceMark, &panel)) {
      g->code = "BAD_MARK";
      return;
    }
    void* player = server->GetPlayer(1);
    Vector eye;
    if (!player || !PlayerEye(&eye)) {
      g->code = "NO_PLAYER";
      return;
    }
    auto wpn = SE(player)->active_weapon();
    uintptr_t gun = (uintptr_t)entityList->LookupEntity(wpn);
    if (!gun || !entityList->IsPortalGun(wpn)) {
      g->code = "NO_GUN";
      return;
    }
    g->linkage = SE(gun)->field<unsigned char>("m_iPortalLinkageGroupID");
    if (!entityList->LookupEntity(
            SE(gun)->field<CBaseHandle>("m_hPrimaryPortal"))) {
      auto b = server->FindPortal(g->linkage, false, true);
      SE(gun)->field<CBaseHandle>("m_hPrimaryPortal") =
          ((IHandleEntity*)b)->GetRefEHandle();
    }
    if (!entityList->LookupEntity(
            SE(gun)->field<CBaseHandle>("m_hSecondaryPortal"))) {
      auto o = server->FindPortal(g->linkage, true, true);
      SE(gun)->field<CBaseHandle>("m_hSecondaryPortal") =
          ((IHandleEntity*)o)->GetRefEHandle();
    }

    Vector dir;
    Math::AngleVectors(aim->view, &dir);
    TracePortalPlacementInfo_t pinfo;
    int ret = server->TraceFirePortal(gun, eye, dir, orange, 2, pinfo);
    int res = (int)pinfo.ePlacementResult;
    if (ret == 0) return;  // NO_LOS: the shot hit nothing placeable
    if (res > (int)PORTAL_PLACEMENT_BUMPED) {
      g->code = PortalRejectCode(res);
      return;
    }
    if (!PortalOnPanel(pinfo.finalPos, panel)) return;  // shot landed off-panel

    char cmd[160];
    std::snprintf(cmd, sizeof(cmd),
                  "portal_place %d %d %.6f %.6f %.6f %.6f %.6f %.6f",
                  (int)g->linkage, orange ? 1 : 0, pinfo.finalPos.x,
                  pinfo.finalPos.y, pinfo.finalPos.z, pinfo.finalAngle.x,
                  pinfo.finalAngle.y, pinfo.finalAngle.z);
    engine->ExecuteCommand(cmd);
    g->placed = pinfo.finalPos;
    g->usedHelper = (res == (int)PORTAL_PLACEMENT_USED_HELPER);
    g->code = "PLACED";
  });
  if (!ran) return cancelled();
  if (g->code != "PLACED") {
    r.set_ok(false);
    r.set_result_code(g->code);
    r.set_detail("place_portal: " + g->code);
    return r;
  }

  // 2. Settle so the portal activates + auto-links, then read m_bActivated
  // back.
  AdvanceTicksBlocking(kSettle);
  auto active = std::make_shared<bool>(false);
  if (!RunOnMainThreadSync(context_, [active, g, orange]() {
        void* portal = (void*)server->FindPortal(g->linkage, orange, false);
        if (portal) *active = SE(portal)->field<bool>("m_bActivated");
      }))
    return cancelled();

  r.set_ok(true);
  r.set_result_code("PLACED");
  r.set_detail(Utils::ssprintf("place_portal: %s at (%.0f %.0f %.0f)%s%s",
                               color.c_str(), g->placed.x, g->placed.y,
                               g->placed.z, g->usedHelper ? " (helper)" : "",
                               *active ? "" : " [inactive]"));
  return r;
}

portal2_harness::MacroResult MacroExecutor::PassThrough(
    const portal2_harness::MacroRequest& req) {
  portal2_harness::MacroResult r;
  TargetRef ref = ClassifyTarget(req.target());
  if (ref.kind != TargetRef::PORTAL) {
    r.set_ok(false);
    r.set_result_code(ref.kind == TargetRef::NONE ? "NO_SUCH_PORTAL"
                                                  : "WRONG_TARGET");
    r.set_detail("pass_through needs a portal Pb/Po, got \"" + req.target() +
                 "\"");
    return r;
  }
  bool orange = ref.orange;

  auto cancelled = []() {
    portal2_harness::MacroResult c;
    c.set_ok(false);
    c.set_result_code("CANCELLED");
    return c;
  };

  // 1. Resolve the portal + its linked partner and the approach front cell.
  struct Setup {
    std::string code = "NO_SUCH_PORTAL";
    Vector center{0, 0, 0}, front{0, 0, 0}, startFeet{0, 0, 0};
  };
  auto s = std::make_shared<Setup>();
  if (!RunOnMainThreadSync(context_, [s, orange]() {
        LivePortal lp = ReadPortal(orange);
        if (!lp.active) return;
        s->code = "UNLINKED";
        if (!lp.linked || !ReadPortal(!orange).active) return;
        ServerEnt* pl = server->GetPlayer(1);
        if (!pl) {
          s->code = "NO_PLAYER";
          return;
        }
        s->center = lp.center;
        s->front = lp.center + lp.normal * kMouthStandoff;
        s->startFeet = pl->abs_origin();
        s->code = "OK";
      }))
    return cancelled();
  if (s->code != "OK") {
    r.set_ok(false);
    r.set_result_code(s->code);
    r.set_detail("pass_through: " + s->code);
    return r;
  }

  // 2. Walk to the mouth's front cell (flood follower). BLOCKED only if it
  // leaves us too far to reach the disc by pushing.
  FollowOutcome m = FollowTo(context_, s->front, kReachRadius, kGoToMaxTicks);
  if (m.cancelled) return cancelled();
  if (!m.reached && m.dist > kMouthReach) {
    r.set_ok(false);
    r.set_result_code("BLOCKED");
    r.set_detail("pass_through: could not reach the mouth");
    return r;
  }

  // 3. Push straight into the mouth until the engine transits.
  auto prevFeet = std::make_shared<Vector>(s->startFeet);
  RunOnMainThreadSync(context_, [prevFeet]() {
    ServerEnt* pl = server->GetPlayer(1);
    if (pl) *prevFeet = pl->abs_origin();
  });
  auto emerged = std::make_shared<Vector>(*prevFeet);
  bool transited = false;
  for (int t = 0; t < kPassMaxTicks; t += kPassBatch) {
    auto feet = std::make_shared<Vector>();
    auto jumped = std::make_shared<bool>(false);
    if (!RunOnMainThreadSync(context_, [feet, jumped, prevFeet, s]() {
          ServerEnt* pl = server->GetPlayer(1);
          if (!pl) return;
          *feet = pl->abs_origin();
          if (Vector{feet->x - prevFeet->x, feet->y - prevFeet->y,
                     feet->z - prevFeet->z}
                  .Length() > kPassJump)
            *jumped = true;
          Vector toMouth{s->center.x - feet->x, s->center.y - feet->y, 0};
          Vector up{0, 0, 1};
          QAngle a;
          Math::VectorAngles(toMouth, up, &a);
          ApplyAbsoluteView(QAngle{0, a.y, 0});
          SetMoveFramebulk(0, 1);  // straight into the mouth, no wall-avoid
        }))
      return cancelled();
    if (*jumped) {
      transited = true;
      *emerged = *feet;
      break;
    }
    *prevFeet = *feet;
    AdvanceTicksBlocking(kPassBatch);
  }

  // Clear the push input but keep m_vecVelocity: the player emerges with its
  // real momentum so a follow-up verb (or `wait`) reasons about the true motion
  // state; ground friction bleeds a grounded walk-through over the settle.
  RunOnMainThreadSync(context_, []() { SetMoveFramebulk(0, 0); });
  AdvanceTicksBlocking(kSettle);

  if (!transited) {
    r.set_ok(false);
    r.set_result_code("NOT_AT_MOUTH");
    r.set_detail("pass_through: never entered the mouth (no transit)");
    return r;
  }

  // 4. Pose consistent with the pair transform: emerged in front of the
  // partner.
  auto ok = std::make_shared<bool>(false);
  auto pos = std::make_shared<Vector>(*emerged);
  RunOnMainThreadSync(context_, [ok, pos, orange]() {
    ServerEnt* pl = server->GetPlayer(1);
    if (pl) *pos = pl->abs_origin();
    LivePortal partner = ReadPortal(!orange);
    if (!partner.active) return;
    *ok = Vector{pos->x - partner.center.x, pos->y - partner.center.y, 0}
              .Length2D() < kEmergeRadius;
  });

  r.set_ok(*ok);
  r.set_result_code(*ok ? "TRANSITED" : "NOT_AT_MOUTH");
  r.set_detail(Utils::ssprintf("pass_through: %s at (%.0f %.0f %.0f)",
                               *ok ? "TRANSITED" : "emerged off-partner",
                               pos->x, pos->y, pos->z));
  return r;
}

portal2_harness::MacroResult MacroExecutor::JumpInto(
    const std::string& target) {
  portal2_harness::MacroResult r;
  TargetRef ref = ClassifyTarget(target);
  if (ref.kind != TargetRef::PORTAL) {
    r.set_ok(false);
    r.set_result_code(ref.kind == TargetRef::NONE ? "NO_SUCH_PORTAL"
                                                  : "WRONG_TARGET");
    r.set_detail("jump_into needs a portal Pb/Po, got \"" + target + "\"");
    return r;
  }
  bool orange = ref.orange;

  auto cancelled = []() {
    portal2_harness::MacroResult c;
    c.set_ok(false);
    c.set_result_code("CANCELLED");
    return c;
  };

  // 1. Resolve the portal + gate on an up-facing normal (a floor portal you
  // fall INTO; a wall portal is pass_through's job).
  struct Setup {
    std::string code = "NO_SUCH_PORTAL";
    Vector center{0, 0, 0};
  };
  auto s = std::make_shared<Setup>();
  if (!RunOnMainThreadSync(context_, [s, orange]() {
        LivePortal lp = ReadPortal(orange);
        if (!lp.active) return;
        if (lp.normal.z < kFloorNormalZ) {
          s->code = "NOT_GROUND";
          return;
        }
        s->center = lp.center;
        s->code = "OK";
      }))
    return cancelled();
  if (s->code != "OK") {
    r.set_ok(false);
    r.set_result_code(s->code);
    r.set_detail("jump_into: " + s->code);
    return r;
  }
  Vector center = s->center;

  // 2. Position tracking + a transit detector (a huge single-tick position
  // jump). Set up BEFORE the approach so a transit at ANY point is caught --
  // running flat onto a ground portal, or the funnel drop -- and never lost.
  auto prev = std::make_shared<Vector>();
  auto exitPos = std::make_shared<Vector>();
  auto entryPos =
      std::make_shared<Vector>();  // pos the tick before the transit
  bool transited = false;
  RunOnMainThreadSync(context_, [prev]() {
    ServerEnt* pl = server->GetPlayer(1);
    if (pl) *prev = pl->abs_origin();
  });
  auto detect = [&]() -> int {  // 1 transited, 0 not, -1 stream dropped
    auto j = std::make_shared<bool>(false);
    if (!RunOnMainThreadSync(context_, [prev, j, exitPos, entryPos]() {
          ServerEnt* pl = server->GetPlayer(1);
          if (!pl) return;
          Vector p = pl->abs_origin();
          if ((p - *prev).Length() > kJumpTransit) {
            *j = true;
            *entryPos = *prev;
            *exitPos = p;
          }
          *prev = p;
        }))
      return -1;
    return *j ? 1 : 0;
  };

  // Flight diagnostics: closest we get to the mouth center + how high we get
  // above it. On a miss these say whether we overshot, undershot, or the funnel
  // just never closed the last few units.
  float minHoriz = 1e9f, maxUp = -1e9f;
  auto trackFlight = [&]() {
    minHoriz = std::min(
        minHoriz, Vector{prev->x - center.x, prev->y - center.y, 0}.Length2D());
    maxUp = std::max(maxUp, prev->z - center.z);
  };

  // 3. Approach the mouth (gentle forward + aim) until within jump range, and
  // stop SHORT so the jump, not a walk, enters (walking flat onto a ground
  // portal transits here, which detect() catches).
  for (int t = 0; t < kJumpRunTicks; ++t) {
    auto h = std::make_shared<float>(-1.0f);
    if (!RunOnMainThreadSync(context_, [center, h]() {
          ServerEnt* pl = server->GetPlayer(1);
          if (!pl) return;
          Vector f = pl->abs_origin();
          *h = Vector{f.x - center.x, f.y - center.y, 0}.Length2D();
          Vector eye;
          if (PlayerEye(&eye)) ApplyAbsoluteView(AimAnglesTo(eye, center));
          SetMoveFramebulk(0, kJumpMoveSpeed);
        }))
      return cancelled();
    if (*h <= kJumpApproach) break;  // close enough -- jump from here
    AdvanceTicksBlocking(1);
    int tr = detect();
    if (tr < 0) return cancelled();
    if (tr == 1) {
      transited = true;
      break;
    }
  }

  // 4. Jump into it -- keep the gentle forward (arcs us over the mouth) + add
  // height. A slower approach leaves less horizontal for the funnel to zero.
  if (!transited) {
    if (!RunOnMainThreadSync(context_, [center]() {
          Vector eye;
          if (!PlayerEye(&eye)) return;
          ApplyAbsoluteView(AimAnglesTo(eye, center));
          SetMoveFramebulk(0, kJumpMoveSpeed);  // clears buttons
          tasPlayer->playbackInfo.slots[0].framebulks[0].buttonStates[Jump] =
              true;
        }))
      return cancelled();
    AdvanceTicksBlocking(kJumpHold);
    int tr = detect();
    if (tr < 0) return cancelled();
    transited = (tr == 1);
    trackFlight();
  }

  // 5. Funnel descent: re-assert the down-aim every tick (the view drifts
  // otherwise, killing the funnel), free-fall, until transit.
  int flightTicks = kJumpHold;
  for (int t = 0; !transited && t < kJumpMaxTicks; ++t) {
    if (!RunOnMainThreadSync(context_, [center]() {
          Vector eye;
          if (PlayerEye(&eye)) ApplyAbsoluteView(AimAnglesTo(eye, center));
        }))
      return cancelled();
    AdvanceTicksBlocking(1);
    int tr = detect();
    if (tr < 0) return cancelled();
    transited = (tr == 1);
    trackFlight();
    flightTicks = kJumpHold + t + 1;
  }

  if (!transited) {
    RunOnMainThreadSync(context_, []() { ClearFramebulk(); });
    r.set_ok(false);
    r.set_result_code("NOT_ALIGNED");
    r.set_detail(Utils::ssprintf(
        "jump_into: no transit -- closest %.0fu from mouth, peak %.0fu up, "
        "ended (%.0f %.0f %.0f)",
        minHoriz, maxUp, prev->x, prev->y, prev->z));
    return r;
  }

  // 6. FREEZE mid-flight: clear the input but LEAVE m_vecVelocity (the fling
  // momentum), and stop advancing -- the game is now paused for the model to
  // reason about the airborne state. `wait N` resumes gravity/flight.
  auto vel = std::make_shared<Vector>();
  RunOnMainThreadSync(context_, [vel]() {
    SetMoveFramebulk(0, 0);
    ServerEnt* pl = server->GetPlayer(1);
    if (pl) *vel = pl->field<Vector>("m_vecVelocity");
  });
  r.set_ok(true);
  r.set_result_code("FLUNG");
  r.set_detail(Utils::ssprintf(
      "jump_into: flung after %d ticks, (%.0f %.0f %.0f) -> (%.0f %.0f %.0f) "
      "vel=(%.0f %.0f %.0f)",
      flightTicks, entryPos->x, entryPos->y, entryPos->z, exitPos->x,
      exitPos->y, exitPos->z, vel->x, vel->y, vel->z));
  return r;
}

// drop_into <Pb|Po> [mark] -- enter a FLOOR portal WITHOUT a jump (self), or
// drop a held object into it (object arm, mark on `aim`). Unlike jump_into
// there is no jump impulse, so what emerges is a gentle exit, not a fling. Both
// arms end AT the transit: input cleared, m_vecVelocity untouched, ticks
// stopped -- the world is paused for the model, and `wait N` resumes physics.
portal2_harness::MacroResult MacroExecutor::DropInto(
    const portal2_harness::MacroRequest& req) {
  portal2_harness::MacroResult r;
  const std::string& target = req.target();
  TargetRef ref = ClassifyTarget(target);
  if (ref.kind != TargetRef::PORTAL) {
    r.set_ok(false);
    r.set_result_code(ref.kind == TargetRef::NONE ? "NO_SUCH_PORTAL"
                                                  : "WRONG_TARGET");
    r.set_detail("drop_into needs a portal Pb/Po, got \"" + target + "\"");
    return r;
  }
  bool orange = ref.orange;
  bool objectArm = !req.aim().empty();

  auto cancelled = []() {
    portal2_harness::MacroResult c;
    c.set_ok(false);
    c.set_result_code("CANCELLED");
    return c;
  };

  // 1. Resolve the floor portal + its linked partner (both arms fall INTO a
  // ground portal; the object arm needs the partner to confirm emergence).
  struct Setup {
    std::string code = "NO_SUCH_PORTAL";
    Vector center{0, 0, 0}, normal{0, 0, 0}, partner{0, 0, 0};
  };
  auto s = std::make_shared<Setup>();
  if (!RunOnMainThreadSync(context_, [s, orange]() {
        LivePortal lp = ReadPortal(orange);
        if (!lp.active) return;
        if (lp.normal.z < kFloorNormalZ) {
          s->code = "NOT_GROUND";
          return;
        }
        LivePortal partner = ReadPortal(!orange);
        if (!lp.linked || !partner.active) {
          s->code = "UNLINKED";
          return;
        }
        s->center = lp.center;
        s->normal = lp.normal;
        s->partner = partner.center;
        s->code = "OK";
      }))
    return cancelled();
  if (s->code != "OK") {
    r.set_ok(false);
    r.set_result_code(s->code);
    r.set_detail("drop_into: " + s->code);
    return r;
  }
  Vector center = s->center;

  if (objectArm) {
    // OBJECT ARM: carry the held object to a stand-off on our side of the
    // mouth, drop it steep-down over the disc, and track ITS transit. If the
    // player crosses the disc instead, that's FELL_THROUGH.
    uint32_t heldKey = g_heldEntityKey.load();
    if (!heldKey) {
      r.set_ok(false);
      r.set_result_code("NOT_HOLDING");
      r.set_detail("drop_into: nothing held -- pick_up the object first");
      return r;
    }
    Vector front = center + s->normal * kDropStandoff;
    Vector partnerCenter = s->partner;

    FollowOutcome m = FollowTo(context_, front, kReachRadius, kGoToMaxTicks);
    if (m.cancelled) return cancelled();
    if (!m.reached) {
      r.set_ok(false);
      r.set_result_code("BLOCKED");
      r.set_detail(Utils::ssprintf(
          "drop_into: carry blocked %.0fu short of the mouth", m.dist));
      return r;
    }

    // Aim steep-down at the mouth center (swings the held object out over the
    // disc), then +use-drop it.
    auto view = std::make_shared<QAngle>();
    RunOnMainThreadSync(context_, [view, center]() {
      Vector eye;
      if (PlayerEye(&eye)) *view = AimAnglesTo(eye, center);
    });
    g_heldEntityKey = 0;  // hands empty for the drop
    if (!DropHeld(context_, *view, kReleaseDropSettle)) {
      g_heldEntityKey = heldKey;  // never released -- keep held-state truthful
      r.set_ok(false);
      r.set_result_code("NOT_IN");
      r.set_detail("drop_into: could not release the held object");
      return r;
    }

    // Track the object's single-tick position jump (a portal transit) while
    // watching the player never crosses the disc.
    auto prevObj = std::make_shared<Vector>();
    auto prevFeet = std::make_shared<Vector>();
    RunOnMainThreadSync(context_, [prevObj, prevFeet, heldKey]() {
      ServerEnt* obj = EntFromKey(heldKey);
      ServerEnt* pl = server->GetPlayer(1);
      if (obj) *prevObj = obj->abs_origin();
      if (pl) *prevFeet = pl->abs_origin();
    });
    auto exitObj = std::make_shared<Vector>(*prevObj);
    auto exitVel = std::make_shared<Vector>();
    bool transited = false, fellThrough = false;
    float minHoriz = 1e9f;
    for (int t = 0; !transited && !fellThrough && t < kJumpMaxTicks; ++t) {
      AdvanceTicksBlocking(1);
      auto res = std::make_shared<int>(0);  // 1 obj transit, 2 player transit
      if (!RunOnMainThreadSync(
              context_, [prevObj, prevFeet, exitObj, exitVel, heldKey, res]() {
                ServerEnt* obj = EntFromKey(heldKey);
                ServerEnt* pl = server->GetPlayer(1);
                if (pl) {
                  Vector f = pl->abs_origin();
                  if ((f - *prevFeet).Length() > kJumpTransit) *res = 2;
                  *prevFeet = f;
                }
                if (obj) {
                  Vector p = obj->abs_origin();
                  if (*res != 2 && (p - *prevObj).Length() > kJumpTransit) {
                    *res = 1;
                    *exitObj = p;
                    *exitVel = obj->field<Vector>("m_vecVelocity");
                  }
                  *prevObj = p;
                }
              }))
        return cancelled();
      if (*res == 1) transited = true;
      if (*res == 2) fellThrough = true;
      minHoriz = std::min(
          minHoriz,
          Vector{prevObj->x - center.x, prevObj->y - center.y, 0}.Length2D());
    }

    RunOnMainThreadSync(context_, []() { ClearFramebulk(); });
    if (fellThrough) {
      r.set_ok(false);
      r.set_result_code("FELL_THROUGH");
      r.set_detail("drop_into: the player clipped the disc and transited");
      return r;
    }
    if (!transited) {
      r.set_ok(false);
      r.set_result_code("NOT_IN");
      r.set_detail(Utils::ssprintf(
          "drop_into: object never entered -- closest %.0fu from center, at "
          "(%.0f %.0f %.0f)",
          minHoriz, prevObj->x, prevObj->y, prevObj->z));
      return r;
    }

    // Emerged near the partner? Freeze the object mid-flight (momentum intact)
    // and face it so the outcome is on-screen.
    bool emerged =
        Vector{exitObj->x - partnerCenter.x, exitObj->y - partnerCenter.y, 0}
            .Length2D() < kEmergeRadius;
    LookBackAt(context_, heldKey);
    r.set_ok(emerged);
    r.set_result_code(emerged ? "TRANSITED" : "NOT_IN");
    r.set_detail(Utils::ssprintf(
        "drop_into: object %s at (%.0f %.0f %.0f) vel=(%.0f %.0f %.0f)",
        emerged ? "TRANSITED" : "emerged off-partner", exitObj->x, exitObj->y,
        exitObj->z, exitVel->x, exitVel->y, exitVel->z));
    return r;
  }

  // SELF ARM: walk gently into the floor portal (no jump -> gentle emergence).
  // The transit detector is armed before the first step so the walk-in transit
  // is caught wherever it lands.
  auto prev = std::make_shared<Vector>();
  auto exitPos = std::make_shared<Vector>();
  RunOnMainThreadSync(context_, [prev]() {
    ServerEnt* pl = server->GetPlayer(1);
    if (pl) *prev = pl->abs_origin();
  });
  auto detect = [&]() -> int {  // 1 transited, 0 not, -1 stream dropped
    auto j = std::make_shared<bool>(false);
    if (!RunOnMainThreadSync(context_, [prev, j, exitPos]() {
          ServerEnt* pl = server->GetPlayer(1);
          if (!pl) return;
          Vector p = pl->abs_origin();
          if ((p - *prev).Length() > kJumpTransit) {
            *j = true;
            *exitPos = p;
          }
          *prev = p;
        }))
      return -1;
    return *j ? 1 : 0;
  };

  bool transited = false;
  float minHoriz = 1e9f;
  for (int t = 0; !transited && t < kJumpRunTicks; ++t) {
    if (!RunOnMainThreadSync(context_, [center]() {
          ServerEnt* pl = server->GetPlayer(1);
          if (!pl) return;
          Vector eye;
          if (PlayerEye(&eye)) ApplyAbsoluteView(AimAnglesTo(eye, center));
          SetMoveFramebulk(0, kJumpMoveSpeed);
        }))
      return cancelled();
    AdvanceTicksBlocking(1);
    int tr = detect();
    if (tr < 0) return cancelled();
    transited = (tr == 1);
    minHoriz = std::min(
        minHoriz, Vector{prev->x - center.x, prev->y - center.y, 0}.Length2D());
  }

  if (!transited) {
    RunOnMainThreadSync(context_, []() { ClearFramebulk(); });
    r.set_ok(false);
    r.set_result_code("NOT_AT_MOUTH");
    r.set_detail(Utils::ssprintf(
        "drop_into: never entered the mouth -- closest %.0fu from center, "
        "ended (%.0f %.0f %.0f)",
        minHoriz, prev->x, prev->y, prev->z));
    return r;
  }

  // Freeze at the transit tick: clear the input, LEAVE m_vecVelocity (the
  // emerged momentum), advance nothing. `wait N` resumes the fall.
  auto vel = std::make_shared<Vector>();
  auto pos = std::make_shared<Vector>(*exitPos);
  RunOnMainThreadSync(context_, [vel, pos]() {
    SetMoveFramebulk(0, 0);
    ServerEnt* pl = server->GetPlayer(1);
    if (pl) {
      *vel = pl->field<Vector>("m_vecVelocity");
      *pos = pl->abs_origin();
    }
  });
  r.set_ok(true);
  r.set_result_code("TRANSITED");
  r.set_detail(Utils::ssprintf(
      "drop_into: transited to (%.0f %.0f %.0f) vel=(%.0f %.0f %.0f)", pos->x,
      pos->y, pos->z, vel->x, vel->y, vel->z));
  return r;
}

portal2_harness::MacroResult MacroExecutor::RedirectTo(
    const portal2_harness::MacroRequest& req) {
  portal2_harness::MacroResult r;
  int cubeMark;
  std::string tcode;
  if (!RequireEntityMark(req.target(), &cubeMark, &tcode)) {
    r.set_ok(false);
    r.set_result_code(tcode);
    r.set_detail("redirect_to target must be a cube mark, got \"" +
                 req.target() + "\"");
    return r;
  }
  std::string aim = req.aim();

  // Gate (main thread): the object must be a type-2 reflector cube currently
  // catching a beam, within arm's reach of the player (re-aiming a cube means
  // physically re-placing it -- no across-the-room teleport-rotate); the aim
  // must resolve to a point (any mark -- entity, portal Pb/Po, or panel
  // Sn@u,v). No movement -- yaw-only atom.
  struct Gate {
    std::string code = "BAD_MARK";
    uint32_t cubeKey = 0;
    Vector aimPoint{0, 0, 0};
    float reach = 0;
  };
  auto g = std::make_shared<Gate>();
  bool ran = RunOnMainThreadSync(context_, [g, cubeMark, aim]() {
    CEntInfo* cInfo = nullptr;
    if (!ResolveMarkInfo(cubeMark, &cInfo, &g->code)) return;
    if (!ResolveTarget(aim, &g->aimPoint, &g->code)) return;
    void* cube = cInfo->m_pEntity;
    if (SE(cube)->field<int>("m_nCubeType") != 2) {
      g->code = "NOT_REFLECTOR";
      return;
    }
    if (!CubeOnAnyBeam(cube)) {
      g->code = "NOT_SEATED";  // not catching a beam -> interpose first
      return;
    }
    Vector eye;
    if (!PlayerEye(&eye)) {
      g->code = "NO_PLAYER";
      return;
    }
    g->reach = (EntityCenter(SE(cube)) - eye).Length();
    if (g->reach > kGrabRange) {
      g->code = "OUT_OF_REACH";  // walk to the cube (go_to) before re-aiming
      return;
    }
    auto [idx, ser] = markTable.GetEntityFromMark(cubeMark);
    g->cubeKey = PackEntKey(idx, static_cast<uint16_t>(ser));
    g->code = "SEAT_OK";
  });
  if (!ran) {
    r.set_ok(false);
    r.set_result_code("CANCELLED");
    return r;
  }
  if (g->code != "SEAT_OK") {
    r.set_ok(false);
    r.set_result_code(g->code);
    r.set_detail(
        g->code == "OUT_OF_REACH"
            ? Utils::ssprintf("redirect_to: cube %.0fu away (>%.0f) -- "
                              "go_to it first",
                              g->reach, kGrabRange)
            : "redirect_to: " + g->code);
    return r;
  }

  // Aim the beam at the point. Whether a downstream target lit up is percept
  // state (the observe returns each target's `powered`), so the verb only
  // reports that it aimed, and how far off.
  float residual = 0;
  if (!AimCube(g->cubeKey, g->aimPoint, &residual)) {
    r.set_ok(false);
    r.set_result_code("CANCELLED");
    return r;
  }
  r.set_ok(true);
  r.set_result_code("AIMED");
  r.set_detail(
      Utils::ssprintf("redirect_to: beam aimed (%.1f deg off)", residual));
  return r;
}

portal2_harness::MacroResult MacroExecutor::AimAt(const std::string& target) {
  portal2_harness::MacroResult r;

  // Resolve + aim in one main-thread hop. Heap output so a late (post-cancel)
  // closure run never writes a dead stack slot. `angles` is the commanded view.
  struct AimOut {
    bool ok = false;
    std::string code = "BAD_MARK";
    QAngle angles{0, 0, 0};
  };
  auto out = std::make_shared<AimOut>();
  bool ran = RunOnMainThreadSync(context_, [out, target]() {
    Vector eye;
    if (!PlayerEye(&eye)) {
      out->code = "NO_PLAYER";
      return;
    }
    Vector center;
    if (!ResolveTarget(target, &center, &out->code)) return;
    out->angles = ApplyAbsoluteView(AimAnglesTo(eye, center));
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
    r.set_detail("aim_at could not resolve target " + target);
    return r;
  }

  // Advance one tick so the view rides back on the next AgentLoop Observe. We
  // report the *commanded* angle so a caller can verify camera == aim without a
  // second, cancel-prone main-thread read-back.
  AdvanceTicksBlocking(1);

  r.set_ok(true);
  r.set_result_code("SUCCESS");
  r.set_aim_pitch(out->angles.x);
  r.set_aim_yaw(out->angles.y);
  r.set_detail(
      Utils::ssprintf("aim pitch=%.1f yaw=%.1f", out->angles.x, out->angles.y));
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

portal2_harness::MacroResult MacroExecutor::GoTo(const std::string& target) {
  portal2_harness::MacroResult r;

  // Resolve the target + capture the start feet (for moved_dist), so an early
  // cancel still reports honest telemetry. No standoff math -- the flood
  // already stops the follow at the nearest cell beside a solid target.
  struct Resolve {
    bool ok = false;
    std::string code = "BAD_MARK";
    Vector center{0, 0, 0};
    Vector startFeet{0, 0, 0};
  };
  auto res = std::make_shared<Resolve>();
  bool ran = RunOnMainThreadSync(context_, [res, target]() {
    if (!ResolveTarget(target, &res->center, &res->code)) return;
    res->ok = true;
    ServerEnt* pl = server->GetPlayer(1);
    if (pl) res->startFeet = pl->abs_origin();
  });
  if (!ran) {
    r.set_ok(false);
    r.set_result_code("CANCELLED");
    return r;
  }
  if (!res->ok) {
    r.set_ok(false);
    r.set_result_code(res->code);
    r.set_detail("go_to could not resolve target " + target);
    return r;
  }

  // Follow the flood route to the resolved target (the follower owns the stop).
  // kGoToReach is tight so a follow-up pick_up/interact lands in grab range;
  // the flood's stand cell already sits beside a solid target.
  Vector dest = res->center;
  FollowOutcome m = FollowTo(context_, dest, kGoToReach, kGoToMaxTicks);
  if (m.cancelled) {
    r.set_ok(false);
    r.set_result_code("CANCELLED");
    r.set_final_dist(m.dist);
    return r;
  }
  // finalFeet is unread when the player vanished before the first plan, so a
  // NO_PLAYER move is honestly zero rather than |startFeet| off the origin.
  float moved = m.code == "NO_PLAYER"
                    ? 0.0f
                    : Vector{m.finalFeet.x - res->startFeet.x,
                             m.finalFeet.y - res->startFeet.y, 0}
                          .Length2D();

  // SUCCESS = at the goal. REACHED_PROJECTION = at the nearest reachable point
  // (beside a solid target, or short of an out-of-reach ledge) -- progress the
  // model re-plans from. NO_ROUTE / STUCK are honest failures.
  bool ok = m.reached || m.code == "REACHED_PROJECTION";
  r.set_ok(ok);
  r.set_result_code(m.code);
  r.set_reached(m.reached);
  r.set_final_dist(m.dist);
  r.set_moved_dist(moved);
  if (m.code == "REACHED_PROJECTION")
    r.set_detail(Utils::ssprintf("nearest reachable point (%s), %.0fu to go",
                                 NavReasonWord(m.blockReason), m.dist));
  else
    r.set_detail(Utils::ssprintf("dist=%.0f after follow", m.dist));
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

  // Stop dead: clear the held key AND the walk velocity, then settle, so the
  // move ends where it stopped instead of coasting into the next verb (as go_to
  // does -- Portal puzzle movement has no run-jump speed worth preserving).
  Scheduler::OnMainThread([]() {
    ClearFramebulk();
    ServerEnt* pl = server->GetPlayer(1);
    if (pl) pl->field<Vector>("m_vecVelocity") = {0, 0, 0};
  });
  AdvanceTicksBlocking(kGoToSettle);

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

portal2_harness::MacroResult MacroExecutor::PickUp(const std::string& target) {
  portal2_harness::MacroResult r;
  int mark;
  std::string tcode;
  if (!RequireEntityMark(target, &mark, &tcode)) {
    r.set_ok(false);
    r.set_result_code(tcode);
    r.set_detail("pick_up needs an entity mark, got \"" + target + "\"");
    return r;
  }

  // Resolve + grabbable-class + reach check, snapshot the pre-grab pos (main
  // thread). Heap output survives a post-cancel closure run; the class gate
  // stops a wrong mark (e.g. a button) being +use-pressed by a bad grab.
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
  portal2_harness::MacroResult aim = AimAt(std::to_string(mark));
  if (!aim.ok()) return aim;  // BAD_MARK / NO_PLAYER / CANCELLED bubble up
  PulseUse(kSettle, QAngle{aim.aim_pitch(), aim.aim_yaw(), 0});

  // Confirm the grab against the engine's held handle (m_hAttachedObject): the
  // player must hold THIS mark's entity. dz/moved/dist are telemetry only -- a
  // movement-based check false-negatives a cube that barely moves (one already
  // near hold height, e.g. seated on a button).
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
    ServerEnt* pl = server->GetPlayer(1);
    CBaseHandle h =
        pl ? pl->field<CBaseHandle>("m_hAttachedObject") : CBaseHandle();
    auto [hidx, hser] = markTable.GetEntityFromMark(mark);
    post->held =
        h && h.GetEntryIndex() == hidx && (uint16_t)h.GetSerialNumber() == hser;
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

portal2_harness::MacroResult MacroExecutor::Release(const std::string& target) {
  int mark = 0;  // 0 = no target: drop at the player's feet
  if (!target.empty()) {
    std::string tcode;
    if (!RequireEntityMark(target, &mark, &tcode)) {
      portal2_harness::MacroResult r;
      r.set_ok(false);
      r.set_result_code(tcode);
      r.set_detail("release target must be an entity mark, got \"" + target +
                   "\"");
      return r;
    }
  }

  // Cache the held cube before clearing held-state -- the seat path resolves it
  // again after the drop, once g_heldEntityKey is gone.
  uint32_t heldKey = g_heldEntityKey.load();
  g_heldEntityKey = 0;  // hands empty (self-corrects on next pick_up if wrong)

  auto cancelled = []() {
    portal2_harness::MacroResult r;
    r.set_ok(false);
    r.set_result_code("CANCELLED");
    return r;
  };

  // Orient before dropping: face the mark if given, else look down to drop at
  // the player's feet (e.g. onto a floor button being stood on). Capture the
  // view so PulseUse can HOLD it across the drop -- a single SetAngles drifts
  // pitch to the ceiling and flings the held cube. AimAt returns the commanded
  // (pre-drift) angle.
  QAngle view{0, 0, 0};
  if (mark > 0) {
    portal2_harness::MacroResult aim = AimAt(std::to_string(mark));
    if (!aim.ok()) return aim;  // BAD_MARK / NO_PLAYER / CANCELLED bubble up
    view = QAngle{aim.aim_pitch(), aim.aim_yaw(), 0};
  } else {
    auto v = std::make_shared<QAngle>();
    bool ran = RunOnMainThreadSync(context_, [v]() {
      QAngle cur = engine->GetAngles(Slot());
      *v = ApplyAbsoluteView(QAngle{kReleasePitch, cur.y, 0});
    });
    if (!ran) return cancelled();
    view = *v;
  }

  // A held cube aimed at a button gets seated on it; everything else is the
  // plain drop below.
  bool seatPath = false;
  if (mark > 0 && heldKey) {
    auto isBtn = std::make_shared<bool>(false);
    bool ran = RunOnMainThreadSync(context_, [isBtn, mark]() {
      CEntInfo* info = nullptr;
      std::string code;
      if (ResolveMarkInfo(mark, &info, &code))
        *isBtn = IsButtonClass(server->GetEntityClassName(info->m_pEntity));
    });
    if (!ran) return cancelled();
    seatPath = *isBtn;
  }

  if (!seatPath) {
    // Drop and CONFIRM the hand emptied: the aimed view first (lands where we
    // looked), then FreeGrab's clear-air sweep -- a steep look-down wedges the
    // cube and +use won't release it. STILL_HELD if neither frees it.
    bool dropped =
        DropHeld(context_, view, kSettle) || FreeGrab(context_, kSettle);
    portal2_harness::MacroResult r;
    if (!dropped) {
      g_heldEntityKey = heldKey;  // never freed -- keep held-state truthful
      r.set_ok(false);
      r.set_result_code("STILL_HELD");
      r.set_detail("release: +use could not free the held cube");
      return r;
    }
    r.set_ok(true);
    r.set_result_code("SUCCESS");
    r.set_detail(mark > 0 ? Utils::ssprintf("released toward mark %d", mark)
                          : "released (look-down)");
    return r;
  }

  // Seat path: free the grab (clear-air sweep -- the button-aimed view wedges
  // the cube into the floor and +use won't drop it), then teleport the cube
  // dead-centre, let the press register, and confirm it latched and stays.
  if (!FreeGrab(context_, kReleaseDropSettle)) {
    portal2_harness::MacroResult r;
    r.set_ok(false);
    r.set_result_code("NOT_SEATED");
    r.set_detail("release: could not drop the held cube for mark " +
                 std::to_string(mark));
    return r;
  }

  // Hop 1: find the seat. If the player occupies it, displace the player and
  // defer seating until the button rises; otherwise seat the cube now.
  struct SeatStep {
    bool placed = false;      // cube is on the button
    bool displaced = false;   // player stepped off; seat after the button rises
    bool fair = true;         // false => unfair seat; left as a plain drop
    bool noStandoff = false;  // player blocks the seat with nowhere to step
    std::string reason;       // failing fairness gate (when !fair)
    Vector seatOrigin{0, 0, 0};  // where the cube was seated (drift baseline)
  };
  auto step = std::make_shared<SeatStep>();
  bool ran1 = RunOnMainThreadSync(context_, [step, heldKey, mark]() {
    ServerEnt* cube = EntFromKey(heldKey);
    ServerEnt* player = server->GetPlayer(1);
    CEntInfo* binfo = nullptr;
    std::string code;
    if (!cube || !player || !ResolveMarkInfo(mark, &binfo, &code)) return;
    ServerEnt* button = SE(binfo->m_pEntity);
    Seat s = ComputeSeat(button, cube);
    if (!s.ok) return;
    // Only seat what a clean hand-drop from here could have reached; otherwise
    // leave the cube where the drop put it.
    Fairness f = CheckFairness(button, cube, player, s);
    if (!f.fair) {
      step->fair = false;
      step->reason = FairnessFailReason(f);
      return;
    }
    // Step the player off if its hull would overlap the seated cube -- an
    // adjacent player (not just one dead-on the seat) shoves the cube off as
    // the press settles. If there's nowhere to step, don't seat into the
    // player and fake a press; report it. Otherwise defer the seat to Hop 2.
    Vector pf = player->abs_origin();
    float gapXY = Vector{s.center.x - pf.x, s.center.y - pf.y, 0}.Length2D();
    float overlapR = HalfWidthXY(player) + HalfWidthXY(cube) + kStandoffMargin;
    if (gapXY < overlapR) {
      Vector standoff;
      float clearR = std::max(HalfWidthXY(button), HalfWidthXY(cube));
      if (!FindPlayerStandoff(button->abs_origin(), clearR, player,
                              &standoff)) {
        step->noStandoff = true;
        return;
      }
      SeatEntity(player, standoff, player->abs_angles());
      step->displaced = true;
      return;
    }
    SeatEntity(cube, s.origin, s.angles);
    step->seatOrigin = s.origin;
    step->placed = true;
  });
  if (!ran1) return cancelled();

  if (step->noStandoff) {
    // The drop already put the cube down; finish its settle and report that the
    // player couldn't clear the seat (so it never seated on the button).
    AdvanceTicksBlocking(kSettle);
    portal2_harness::MacroResult r;
    r.set_ok(false);
    r.set_result_code("NOT_SEATED");
    r.set_detail("release: player blocks mark " + std::to_string(mark) +
                 " (no room to step off); dropped instead");
    return r;
  }

  if (!step->fair) {
    // Unfair seat: the drop already put the cube down; finish its settle and
    // report why we didn't place it.
    AdvanceTicksBlocking(kSettle);
    portal2_harness::MacroResult r;
    r.set_ok(false);
    r.set_result_code("NOT_FAIR");
    r.set_detail("release: unfair seat on mark " + std::to_string(mark) + " (" +
                 step->reason + "); dropped instead");
    return r;
  }

  // Hop 2 (only if displaced): the button has risen, so re-find the seat at the
  // rest height and place the cube there.
  if (step->displaced) {
    AdvanceTicksBlocking(kButtonRiseTicks);
    bool ran2 = RunOnMainThreadSync(context_, [step, heldKey, mark]() {
      ServerEnt* cube = EntFromKey(heldKey);
      CEntInfo* binfo = nullptr;
      std::string code;
      if (!cube || !ResolveMarkInfo(mark, &binfo, &code)) return;
      Seat s = ComputeSeat(SE(binfo->m_pEntity), cube);
      if (!s.ok) return;
      SeatEntity(cube, s.origin, s.angles);
      step->seatOrigin = s.origin;
      step->placed = true;
    });
    if (!ran2) return cancelled();
  }

  portal2_harness::MacroResult r;
  if (!step->placed) {
    r.set_ok(false);
    r.set_result_code("NOT_SEATED");
    r.set_detail("release: could not place on mark " + std::to_string(mark));
    return r;
  }

  // Dwell: the cube's m_bActivated must read pressed after the press settles,
  // and still pressed a few ticks later -- a transient touch is not a seat.
  auto readActivated = [this, heldKey]() {
    auto act = std::make_shared<bool>(false);
    RunOnMainThreadSync(context_, [act, heldKey]() {
      ServerEnt* cube = EntFromKey(heldKey);
      if (cube) *act = cube->field<bool>("m_bActivated");
    });
    return *act;
  };
  AdvanceTicksBlocking(kSeatSettle);
  bool act1 = readActivated();
  AdvanceTicksBlocking(kSeatDwellGap);
  // Final read: still pressed AND still on the seat -- a cube that latched then
  // slid off during the dwell must not report SEATED.
  struct Final {
    bool act = false;
    float drift = 0;
    bool onSeat = false;
  };
  auto fin = std::make_shared<Final>();
  RunOnMainThreadSync(context_, [fin, heldKey, step]() {
    ServerEnt* cube = EntFromKey(heldKey);
    if (!cube) return;
    fin->act = cube->field<bool>("m_bActivated");
    Vector o = cube->abs_origin();
    fin->drift = Vector{o.x - step->seatOrigin.x, o.y - step->seatOrigin.y, 0}
                     .Length2D();
    fin->onSeat = fin->drift <= HalfWidthXY(cube);
  });

  // Face the placed cube so the agent sees the outcome (the player was stepped
  // off the seat and is no longer looking at it).
  LookBackAt(context_, heldKey);

  bool seated = act1 && fin->act && fin->onSeat;
  r.set_ok(seated);
  r.set_result_code(seated ? "SEATED" : "NOT_SEATED");
  r.set_detail(
      Utils::ssprintf("release mark %d: m_bActivated %d/%d drift %.0fu", mark,
                      (int)act1, (int)fin->act, fin->drift));
  return r;
}

portal2_harness::MacroResult MacroExecutor::Interact(
    const std::string& target) {
  // +use acts on an entity (a button/switch), not a panel or portal.
  TargetRef ref = ClassifyTarget(target);
  if (ref.kind != TargetRef::ENTITY) {
    portal2_harness::MacroResult r;
    r.set_ok(false);
    r.set_result_code(ref.kind == TargetRef::NONE ? "BAD_MARK"
                                                  : "WRONG_TARGET");
    r.set_detail("interact needs an entity mark, got \"" + target + "\"");
    return r;
  }

  // Walk into reach, face the mark, pulse +use. The engine decides what +use
  // does from world state (press a button, activate a thing); the verb name is
  // the intent. press will alias this once wired -- identical mechanics.
  portal2_harness::MacroResult nav = GoTo(target);
  if (!nav.reached()) {
    // Frame a genuine nav failure as a reachability problem, but pass CANCELLED
    // (a dropped stream, which carries no detail by convention) through clean.
    if (nav.result_code() != "CANCELLED")
      nav.set_detail("interact: not in reach (" + nav.detail() + ")");
    return nav;  // carries BLOCKED / STUCK / BAD_MARK / CANCELLED
  }
  portal2_harness::MacroResult aim = AimAt(target);
  if (!aim.ok()) return aim;
  PulseUse(kSettle, QAngle{aim.aim_pitch(), aim.aim_yaw(), 0});

  portal2_harness::MacroResult r;
  r.set_ok(true);
  r.set_result_code("SUCCESS");
  r.set_detail("interacted with mark " + target);
  return r;
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

  // Current press state -- the authoritative seated signal (cube m_bActivated,
  // which flips in lockstep with the button). After seat_place + N ticks this
  // shows the press latching, so it tells you the settle count the verb needs.
  bool btnState = SE(binfo->m_pEntity)->field<bool>("m_bButtonState");
  bool cubeAct = cube->field<bool>("m_bActivated");
  console->Print("  press    button.m_bButtonState=%d  cube.m_bActivated=%d\n",
                 btnState, cubeAct);

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
    ServerEnt* button = SE(binfo->m_pEntity);
    if (FindPlayerStandoff(button->abs_origin(), HalfWidthXY(button), pl,
                           &standoff))
      console->Print("    standoff     (%.1f, %.1f, %.1f)\n", standoff.x,
                     standoff.y, standoff.z);
    else
      console->Print("    standoff     none found\n");
  }
}

// Debug: snap the held (or given) cube onto a button mark via seat-find +
// CBaseEntity::Teleport, with NO fairness or verify. Mutates state (moves the
// cube). Prefer a free cube-mark: a still-held cube gets yanked back by the
// grab controller on the next tick.
CON_COMMAND(
    sar_harness_seat_place,
    "sar_harness_seat_place <button-mark> [cube-mark] - teleport the "
    "held (or given) cube onto a button (no fairness/verify). Moves the "
    "cube.\n") {
  if (args.ArgC() < 2) {
    console->Print("usage: sar_harness_seat_place <button-mark> [cube-mark]\n");
    return;
  }
  if (!server || !server->GetPlayer(1)) {
    console->Print("seat_place: no player.\n");
    return;
  }
  std::string code;
  int buttonMark = std::atoi(args[1]);
  CEntInfo* binfo = nullptr;
  if (!ResolveMarkInfo(buttonMark, &binfo, &code)) {
    console->Print("seat_place: button mark %d -> %s\n", buttonMark,
                   code.c_str());
    return;
  }
  ServerEnt* cube = nullptr;
  if (args.ArgC() > 2) {
    int cubeMark = std::atoi(args[2]);
    CEntInfo* cinfo = nullptr;
    if (!ResolveMarkInfo(cubeMark, &cinfo, &code)) {
      console->Print("seat_place: cube mark %d -> %s\n", cubeMark,
                     code.c_str());
      return;
    }
    cube = SE(cinfo->m_pEntity);
  } else if (!(cube = HeldCube())) {
    console->Print("seat_place: not holding a cube (pass a cube mark)\n");
    return;
  } else {
    console->Print(
        "seat_place: WARNING using held cube -- the grab may yank it back; "
        "mark a free cube instead.\n");
  }

  Seat s = ComputeSeat(SE(binfo->m_pEntity), cube);
  if (!s.ok) {
    console->Print("seat_place: no press surface under button mark %d\n",
                   buttonMark);
    return;
  }
  SeatEntity(cube, s.origin, s.angles);
  console->Print(
      "seat_place: teleported cube to (%.1f, %.1f, %.1f) ang (p%.1f y%.1f "
      "r%.1f)\n",
      s.origin.x, s.origin.y, s.origin.z, s.angles.x, s.angles.y, s.angles.z);
}

// Debug: seat a free cube on a button via ComputeSeat, yaw-only aim the cube's
// +X at a laser target, and dump the button geometry: whether the button is
// inside the beam's lateral capture radius, whether the seat lands the cube in
// the press trigger z-band (or floats above it), and whether a yaw-only aim
// (pitch never touched) reaches the target. The cube is left FREE (grabbable);
// the press + power are sim-tick events, so read them AFTER with
// sar_harness_dump_fields + sar_harness_laser_probe. Moves the cube; use a free
// cube mark and reload between runs.
CON_COMMAND(
    sar_harness_dual_seat_spike,
    "sar_harness_dual_seat_spike <emitter-mark> <target-mark> <cube-mark> "
    "<button-mark> - seat a free cube on the button and yaw-only aim +X at the "
    "target; print the beam lateral offset, the button trigger band vs the "
    "cube "
    "bottom (float check), and the aim. Read press/power AFTER with "
    "dump_fields "
    "+ laser_probe. Moves the cube.\n") {
  if (args.ArgC() < 5) {
    console->Print(
        "usage: sar_harness_dual_seat_spike <emitter-mark> <target-mark> "
        "<cube-mark> <button-mark>\n");
    return;
  }
  if (!server || !server->GetPlayer(1)) {
    console->Print("dual_seat_spike: no player.\n");
    return;
  }

  std::string code;
  CEntInfo *einfo = nullptr, *tinfo = nullptr, *cinfo = nullptr,
           *binfo = nullptr;
  int em = std::atoi(args[1]), tg = std::atoi(args[2]), cb = std::atoi(args[3]),
      bt = std::atoi(args[4]);
  struct Arg {
    const char* what;
    int mark;
    CEntInfo** out;
  };
  for (const Arg& a : {Arg{"emitter", em, &einfo}, Arg{"target", tg, &tinfo},
                       Arg{"cube", cb, &cinfo}, Arg{"button", bt, &binfo}}) {
    if (!ResolveMarkInfo(a.mark, a.out, &code)) {
      console->Print("dual_seat_spike: %s mark %d -> %s\n", a.what, a.mark,
                     code.c_str());
      return;
    }
  }

  ServerEnt* emitter = SE(einfo->m_pEntity);
  ServerEnt* target = SE(tinfo->m_pEntity);
  ServerEnt* cube = SE(cinfo->m_pEntity);
  ServerEnt* button = SE(binfo->m_pEntity);
  const char* bcls = server->GetEntityClassName(binfo->m_pEntity);

  Seat s = ComputeSeat(button, cube);
  if (!s.ok) {
    console->Print("dual_seat_spike: no press surface under button mark %d\n",
                   bt);
    return;
  }

  // Lateral offset: the button trigger centre vs the un-bent emitter ray, in
  // the horizontal plane. Beyond the beam's ~24u capture radius, one rigid seat
  // cannot both press the button and intercept the beam.
  Vector E = emitter->abs_origin();
  Vector fwd;
  Math::AngleVectors(emitter->abs_angles(), &fwd);
  float fwdLen = std::sqrt(fwd.x * fwd.x + fwd.y * fwd.y);
  Vector fwdH =
      fwdLen > 0 ? Vector{fwd.x / fwdLen, fwd.y / fwdLen, 0} : Vector{1, 0, 0};
  Vector relH{s.center.x - E.x, s.center.y - E.y, 0};
  float along = relH.x * fwdH.x + relH.y * fwdH.y;
  Vector perp{relH.x - fwdH.x * along, relH.y - fwdH.y * along, 0};
  float lateral = std::sqrt(perp.x * perp.x + perp.y * perp.y);

  // Yaw-only aim: +X at the target from the seat centre; pitch stays flat. The
  // target's elevation off horizontal is the error a yaw-only aim cannot fix.
  Vector tc = EntityCenter(target);
  float yaw =
      NormalizeYaw(RAD2DEG(std::atan2(tc.y - s.center.y, tc.x - s.center.x)));
  Vector d = tc - s.center;
  float horiz = std::sqrt(d.x * d.x + d.y * d.y);
  float pitchErr = RAD2DEG(std::atan2(d.z, horiz));

  SeatEntity(cube, s.origin, QAngle{0, yaw, 0});

  // Button geometry, to explain a float: where the press TRIGGER volume sits vs
  // the surface ComputeSeat's player-only down-trace hit vs the surface under
  // the button (skipping it). The cube bottom lands at surface - kSeatBias; if
  // that is above the trigger top, the cube floats and never presses.
  ICollideable& bcoll = button->collision();
  Vector bMin = bcoll.OBBMins(), bMax = bcoll.OBBMaxs();
  Vector trigMin = button->abs_origin(), trigMax = button->abs_origin();
  bcoll.WorldSpaceTriggerBounds(&trigMin, &trigMax);
  Vector probeStart{s.center.x, s.center.y,
                    button->abs_origin().z + bMax.z + 64.0f};
  QAngle probeDown{90, 0, 0};
  TraceSkip2 skipPB;
  skipPB.SetPassEntity(server->GetPlayer(1));
  skipPB.skip2 = button;
  CGameTrace underTr;
  bool underHit = engine->Trace(probeStart, probeDown, 256.0f, MASK_PLAYERSOLID,
                                skipPB, underTr);
  float cubeBottom = s.surface.z - kSeatBias;
  bool inTrigger = cubeBottom <= trigMax.z && (s.center.z) >= trigMin.z;
  int cubeType = cube->field<int>("m_nCubeType");

  console->Print(
      "dual_seat_spike: emitter %d  target %d  cube %d  button %d (%s)\n", em,
      tg, cb, bt, bcls);
  console->Print("  beam lateral offset = %.1fu  %s\n", lateral,
                 lateral <= 24.0f ? "[within +-24u capture radius]"
                                  : "[OFF-RAY -> one seat may not do both]");
  console->Print(
      "  button origin.z=%.2f  OBB z=[%.2f,%.2f]  TRIGGER z=[%.2f,%.2f]\n",
      button->abs_origin().z, button->abs_origin().z + bMin.z,
      button->abs_origin().z + bMax.z, trigMin.z, trigMax.z);
  console->Print(
      "  down-trace player-only=%.2f  under-button=%.2f%s  cube bottom=%.2f\n",
      s.surface.z, underHit ? underTr.endpos.z : 0.0f,
      underHit ? "" : " (miss)", cubeBottom);
  console->Print("  seat center (%.1f %.1f %.1f)  m_nCubeType=%d  FLOAT: %s\n",
                 s.center.x, s.center.y, s.center.z, cubeType,
                 inTrigger ? "cube spans the trigger band"
                           : "cube ABOVE the trigger -> floats, no press");
  console->Print(
      "  yaw-only aim yaw=%.1f  target pitch err=%.1f  %s\n", yaw, pitchErr,
      std::fabs(pitchErr) <= 3.0f ? "[yaw-only reaches it]"
                                  : "[non-coplanar -> yaw-only may miss]");
  console->Print(
      "  cube is FREE (grabbable, will settle). press/power are sim-tick "
      "events "
      "-- read button.m_bButtonState / target.m_bPowered with "
      "sar_harness_dump_fields + sar_harness_laser_probe after a moment.\n");
}

// Dryrun of interpose's seat+reachability gates (no carry/teleport). Takes RAW
// entity indices (no marks) -- the emitter and the cube to seat -- and prints
// the gate code + computed seat. The verb itself resolves the emitter by mark
// and the cube from the hand.
CON_COMMAND(sar_harness_interpose_dryrun,
            "sar_harness_interpose_dryrun <emitter_idx> <cube_idx> <percent> - "
            "run interpose's seat+reachability gates (no movement) and print "
            "the result code. Raw entity indices (sar_harness_laser_probe); "
            "percent in [0,1] along the beam.\n") {
  if (!server || !entityList) {
    console->Print("interpose dryrun: no server/entity list yet.\n");
    return;
  }
  if (args.ArgC() < 4) {
    console->Print(
        "usage: sar_harness_interpose_dryrun <emitter_idx> <cube_idx> "
        "<percent>\n");
    return;
  }
  auto getEnt = [](int idx) -> void* {
    if (idx < 0 || idx >= Offsets::NUM_ENT_ENTRIES) return nullptr;
    auto info = entityList->GetEntityInfoByIndex(idx);
    return (info && info->m_pEntity) ? info->m_pEntity : nullptr;
  };
  void* emitter = getEnt(std::atoi(args[1]));
  void* cube = getEnt(std::atoi(args[2]));
  if (!emitter || !cube) {
    console->Print("interpose dryrun: bad emitter/cube index.\n");
    return;
  }
  float percent = (float)std::atof(args[3]);
  Vector seat{0, 0, 0};
  float beamLen = 0;
  std::string code = InterposeGate(emitter, cube, percent, &seat, &beamLen);
  console->Print("interpose dryrun: %s\n", code.c_str());
  if (code == "SEAT_OK")
    console->Msg("  seat (%.1f %.1f %.1f)  beam len %.1f  percent %.2f\n",
                 seat.x, seat.y, seat.z, beamLen, percent);
}

// Run interpose's snap end-to-end in-console: gate + teleport-snap +
// interception re-trace, on a FREE cube by raw index (no carry -- that needs
// the harness/Python). percent in [0,1]; read the settled result with
// sar_harness_laser_probe.
CON_COMMAND(
    sar_harness_interpose_run,
    "sar_harness_interpose_run <emitter_idx> <cube_idx> <percent> [target_idx] "
    "- "
    "gate + teleport-snap a FREE cube onto the beam + confirm interception (no "
    "carry); optional target_idx yaws the cube at it. Raw indices; percent "
    "[0,1].\n") {
  if (!server || !entityList) {
    console->Print("interpose run: no server/entity list yet.\n");
    return;
  }
  if (args.ArgC() < 4) {
    console->Print(
        "usage: sar_harness_interpose_run <emitter_idx> <cube_idx> "
        "<percent>\n");
    return;
  }
  auto getEnt = [](int idx) -> void* {
    if (idx < 0 || idx >= Offsets::NUM_ENT_ENTRIES) return nullptr;
    auto info = entityList->GetEntityInfoByIndex(idx);
    return (info && info->m_pEntity) ? info->m_pEntity : nullptr;
  };
  void* emitter = getEnt(std::atoi(args[1]));
  void* cube = getEnt(std::atoi(args[2]));
  if (!emitter || !cube) {
    console->Print("interpose run: bad emitter/cube index.\n");
    return;
  }
  float percent = (float)std::atof(args[3]);
  Vector seat{0, 0, 0};
  float beamLen = 0;
  std::string code = InterposeGate(emitter, cube, percent, &seat, &beamLen);
  if (code != "SEAT_OK") {
    console->Print("interpose run: %s (no placement)\n", code.c_str());
    return;
  }
  // Optional 4th arg: a target index -> yaw the cube's +X at it. m_bPowered is
  // a sim-tick event the console can't AdvanceTicks for; read it after with
  // sar_harness_laser_probe.
  void* target = args.ArgC() > 4 ? getEnt(std::atoi(args[4])) : nullptr;
  QAngle yaw = target ? ComputeRedirectYaw(seat, SE(target)->abs_origin())
                      : QAngle{0, 0, 0};
  SeatEntity(cube, seat, yaw);  // teleport-snap, aimed if a target was given
  bool intercept = ConfirmInterception(emitter, cube);
  console->Print(
      "interpose run: snapped to (%.1f %.1f %.1f) len %.1f%s -> %s\n", seat.x,
      seat.y, seat.z, beamLen, target ? " (aimed)" : "",
      intercept ? "ON_BEAM" : "NOT_INTERCEPTING");
  if (target)
    console->Msg(
        "  yawed at target [%d]; read m_bPowered via sar_harness_laser_probe\n",
        std::atoi(args[4]));
}
