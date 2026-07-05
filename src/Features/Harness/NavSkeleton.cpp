#include "NavSkeleton.hpp"

#include <algorithm>
#include <cmath>

#include "BspFilePanelSource.hpp"
#include "Command.hpp"
#include "Entity.hpp"
#include "Modules/Console.hpp"
#include "Modules/Engine.hpp"
#include "Modules/Server.hpp"
#include "Offsets.hpp"
#include "Utils/SDK/EntityEdict.hpp"
#include "Utils/SDK/Trace.hpp"

namespace {
// Height bands for a step between adjacent floor surfaces (tunable; the path
// visualizer is the arbiter). Up beyond kStepUp needs a jump/fling (no edge);
// down beyond kMaxDrop is unsafe (no edge).
constexpr float kWalkFlat = 8.0f;   // |dz| <= this: same level
constexpr float kStepUp = 24.0f;    // walkable step up
constexpr float kMaxDrop = 128.0f;  // walk off a ledge down
constexpr float kAdjGap = 4.0f;  // XY AABBs within this (both axes): adjacent

bool XyAdjacent(const NavSkeleton::Surface& a, const NavSkeleton::Surface& b) {
  return a.mins.x - kAdjGap <= b.maxs.x && b.mins.x - kAdjGap <= a.maxs.x &&
         a.mins.y - kAdjGap <= b.maxs.y && b.mins.y - kAdjGap <= a.maxs.y;
}

// Edge type stepping from fromZ to toZ; 0xFF = not connectable on foot.
uint8_t BandEdge(float fromZ, float toZ) {
  float dz = toZ - fromZ;
  if (std::fabs(dz) <= kWalkFlat) return NavSkeleton::WALK;
  if (dz > 0) return dz <= kStepUp ? NavSkeleton::STEP_UP : 0xFF;
  if (-dz <= kStepUp) return NavSkeleton::STEP_DOWN;
  return -dz <= kMaxDrop ? NavSkeleton::DROP : 0xFF;
}

const char* EdgeName(uint8_t t) {
  switch (t) {
    case NavSkeleton::WALK:
      return "walk";
    case NavSkeleton::STEP_UP:
      return "up";
    case NavSkeleton::STEP_DOWN:
      return "down";
    case NavSkeleton::DROP:
      return "drop";
    default:
      return "?";
  }
}

// A player hull can rest somewhere on the surface. Sample a grid across its
// bounds (not just the center, so a floor wrapping a pillar isn't rejected
// because its bbox center lands on the pillar) and keep it if any point has
// solid floor just under it (rejects no-collision faces) and the standing hull
// fits there (not startsolid in a wall / low ceiling / too-steep tilt an
// axis-aligned box can't sit flush on). Main thread (engine traces).
bool CanStand(const Vector& bmin, const Vector& bmax, float z,
              const Vector& hmin, const Vector& hmax) {
  CTraceFilterSimple filter;
  filter.SetPassEntity(server->GetPlayer(1));
  for (float fx = 0.2f; fx <= 0.81f; fx += 0.3f)
    for (float fy = 0.2f; fy <= 0.81f; fy += 0.3f) {
      float x = bmin.x + (bmax.x - bmin.x) * fx;
      float y = bmin.y + (bmax.y - bmin.y) * fy;
      Vector top{x, y, z + 8.0f};
      QAngle down{90, 0, 0};
      CGameTrace tr;
      if (!engine->Trace(top, down, 24.0f, MASK_PLAYERSOLID, filter, tr))
        continue;
      Vector at{x, y, tr.endpos.z + 2.0f};
      CGameTrace hull;
      if (!engine->TraceHull(at, at, hmin, hmax, MASK_PLAYERSOLID, filter,
                             hull))
        return true;
    }
  return false;
}
}  // namespace

void NavSkeleton::Build(const std::string& mapName) {
  surfaces_.clear();
  edges_.clear();
  gates_.clear();
  // Player hull for the walkability gate; skip the gate if there's no live
  // player to size/seat it against (keep the raw BSP candidates).
  ServerEnt* pl = server ? server->GetPlayer(1) : nullptr;
  Vector hmins, hmaxs;
  if (pl) {
    hmins = pl->collision().OBBMins();
    hmaxs = pl->collision().OBBMaxs();
  }
  uint32_t id = 0;
  for (const FloorSurface& fs : EnumerateFloorSurfaces(mapName)) {
    if (pl && !CanStand(fs.mins, fs.maxs, fs.z, hmins, hmaxs)) continue;
    Surface s;
    s.id = id++;
    s.normal = fs.normal;
    s.z = fs.z;
    s.mins = fs.mins;
    s.maxs = fs.maxs;
    s.poly = {fs.corners[0], fs.corners[1], fs.corners[2], fs.corners[3]};
    surfaces_.push_back(std::move(s));
  }
  BuildEdges();
  // TODO: dynamic brush surfaces + gates.
}

void NavSkeleton::BuildEdges() {
  edges_.clear();
  for (size_t i = 0; i < surfaces_.size(); ++i)
    for (size_t j = i + 1; j < surfaces_.size(); ++j) {
      const Surface& a = surfaces_[i];
      const Surface& b = surfaces_[j];
      if (!XyAdjacent(a, b)) continue;
      float vx =
          (std::max(a.mins.x, b.mins.x) + std::min(a.maxs.x, b.maxs.x)) * 0.5f;
      float vy =
          (std::max(a.mins.y, b.mins.y) + std::min(a.maxs.y, b.maxs.y)) * 0.5f;
      uint8_t ab = BandEdge(a.z, b.z);
      if (ab != 0xFF)
        edges_.push_back(Edge{a.id, b.id, ab, Vector{vx, vy, a.z}, 0});
      uint8_t ba = BandEdge(b.z, a.z);
      if (ba != 0xFF)
        edges_.push_back(Edge{b.id, a.id, ba, Vector{vx, vy, b.z}, 0});
    }
}

NavSkeleton::PlanResult NavSkeleton::Plan(const Vector& start,
                                          const Vector& target) {
  (void)start;
  PlanResult r;
  r.target = target;
  // TODO: global surface A* + local within-surface routing.
  return r;
}

// Parse the current map's floor surfaces and print each surface's z + XY
// bounds, then the BSP-z vs down-trace delta at the player's feet -- the check
// that BSP floor z is where the body actually stands. Read-only.
CON_COMMAND(sar_harness_nav_dump,
            "sar_harness_nav_dump - list the go_to floor surfaces (z + bounds) "
            "and the BSP-z vs down-trace delta at the player's feet.\n") {
  if (!engine) {
    console->Print("nav_dump: no engine.\n");
    return;
  }
  NavSkeleton nav;
  nav.Build(engine->GetCurrentMapName());
  const std::vector<NavSkeleton::Surface>& surfaces = nav.Surfaces();
  console->Print("nav_dump: %d floor surfaces\n", (int)surfaces.size());
  for (const NavSkeleton::Surface& s : surfaces)
    console->Msg(
        "    #%u z=%.0f n=%.2f,%.2f,%.2f  x[%.0f..%.0f] y[%.0f..%.0f]\n", s.id,
        s.z, s.normal.x, s.normal.y, s.normal.z, s.mins.x, s.maxs.x, s.mins.y,
        s.maxs.y);

  const std::vector<NavSkeleton::Edge>& edges = nav.Edges();
  int byType[6] = {0};
  for (const NavSkeleton::Edge& e : edges)
    if (e.type < 6) byType[e.type]++;
  console->Print("nav_dump: %d edges (walk %d up %d down %d drop %d)\n",
                 (int)edges.size(), byType[NavSkeleton::WALK],
                 byType[NavSkeleton::STEP_UP], byType[NavSkeleton::STEP_DOWN],
                 byType[NavSkeleton::DROP]);
  if (args.ArgC() > 1)  // any arg: also dump the edge list
    for (const NavSkeleton::Edge& e : edges)
      console->Msg("    #%u -%s-> #%u\n", e.from, EdgeName(e.type), e.to);

  ServerEnt* pl = server ? server->GetPlayer(1) : nullptr;
  if (!pl) {
    console->Print("    (no player; load a map for the floorZ delta)\n");
    return;
  }
  Vector feet = pl->abs_origin();
  CTraceFilterSimple filter;
  filter.SetPassEntity(pl);
  Vector top{feet.x, feet.y, feet.z + 40.0f};
  QAngle down{90, 0, 0};
  CGameTrace tr;
  if (!engine->Trace(top, down, 200.0f, MASK_PLAYERSOLID, filter, tr)) {
    console->Print(
        "    feet %.0f %.0f %.0f: no floor under a 200u down-trace\n", feet.x,
        feet.y, feet.z);
    return;
  }
  float traceZ = tr.endpos.z;

  const NavSkeleton::Surface* best = nullptr;
  float bestDz = 1e30f;
  for (const NavSkeleton::Surface& s : surfaces) {
    if (feet.x < s.mins.x || feet.x > s.maxs.x || feet.y < s.mins.y ||
        feet.y > s.maxs.y)
      continue;
    float dz = std::fabs(s.z - traceZ);
    if (dz < bestDz) {
      bestDz = dz;
      best = &s;
    }
  }
  if (!best) {
    console->Print(
        "    feet %.0f %.0f %.0f: trace floor z=%.1f, NO enumerated surface "
        "covers the feet\n",
        feet.x, feet.y, feet.z, traceZ);
    return;
  }
  console->Print(
      "    feet %.0f %.0f %.0f: trace z=%.1f  surface #%u z=%.1f  delta=%.1f\n",
      feet.x, feet.y, feet.z, traceZ, best->id, best->z, best->z - traceZ);
}
