#include "NavSkeleton.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <unordered_map>

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

// Flood lattice. Climb between neighbor cells is capped at engine step height
// on flat ground, but at a full slope-rise when either cell is inclined (a
// walkable-limit 45.57deg slope rises ~33u per 32u cell; a 27.9deg stair ramp
// ~17u). Distinct stacked floors in one column are >=72u apart (hull height),
// so kZSeparate splits them unambiguously.
constexpr float kCell = NavSkeleton::kCellSize;
constexpr float kLift = 2.0f;
constexpr float kStepClimb = 18.0f;
constexpr float kSlopeClimb = 34.0f;
constexpr float kFlatNz = 0.95f;
constexpr float kZSeparate = 36.0f;
constexpr uint32_t kMaxCells = 100000;

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
  std::vector<Vector> seeds;
  if (pl) seeds.push_back(pl->abs_origin());
  for (const FloorSurface& fs : EnumerateFloorSurfaces(mapName)) {
    seeds.push_back(Vector{(fs.mins.x + fs.maxs.x) * 0.5f,
                           (fs.mins.y + fs.maxs.y) * 0.5f, fs.z});
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
  Flood(seeds);
  // TODO: dynamic brush surfaces + gates.
}

// Seeded BFS over the lattice: a cell exists iff the standing player hull
// rests there (down-ray finds a walkable-slope floor, zero-length hull fits
// above it), an edge iff a swept hull passes between neighbor centers at the
// higher floor. The trace is the walkability authority; BSP floor faces only
// seed it. Movers are traced at their live pose. Main thread.
void NavSkeleton::Flood(const std::vector<Vector>& seeds) {
  cells_.clear();
  floodMs_ = 0;
  floodCapped_ = false;
  ServerEnt* pl = server ? server->GetPlayer(1) : nullptr;
  if (!pl || !engine) return;
  auto t0 = std::chrono::steady_clock::now();

  Vector hmin = pl->collision().OBBMins();
  Vector hmax = pl->collision().OBBMaxs();
  hmax.z = std::max(hmax.z, 72.0f);  // standing hull even if built ducked
  CTraceFilterSimple filter;
  filter.SetPassEntity(pl);

  std::unordered_map<uint64_t, std::vector<uint32_t>> columns;
  auto colKey = [](int cx, int cy) {
    return (uint64_t)(uint32_t)cx << 32 | (uint32_t)cy;
  };
  auto center = [](int c) { return c * kCell + kCell * 0.5f; };

  // A box on a slope rests on its uphill corner, above the center-ray hit by
  // up to half-width * slope gradient; the fit and the link sweep must sit
  // that high or any incline past ~7deg reads startsolid.
  float halfW = (hmax.x - hmin.x) * 0.5f;
  auto hullLift = [halfW](float nz) {
    return kLift + halfW * std::sqrt(2.0f * (1.0f - nz * nz)) / nz;
  };

  auto probe = [&](int cx, int cy, float fromZ, float* z, float* nz) {
    Vector top{center(cx), center(cy), fromZ + kSlopeClimb + kLift};
    QAngle down{90, 0, 0};
    CGameTrace tr;
    if (!engine->Trace(top, down, kSlopeClimb + kMaxDrop + 4.0f,
                       MASK_PLAYERSOLID, filter, tr))
      return false;
    if (tr.startsolid || tr.plane.normal.z < 0.7f) return false;
    CGameTrace liq;  // goo/water above the floor: lethal, not walkable
    if (engine->Trace(top, down, kSlopeClimb + kMaxDrop + 4.0f, MASK_WATER,
                      filter, liq) &&
        liq.endpos.z > tr.endpos.z)
      return false;
    Vector at{center(cx), center(cy),
              tr.endpos.z + hullLift(tr.plane.normal.z)};
    CGameTrace hull;
    if (engine->TraceHull(at, at, hmin, hmax, MASK_PLAYERSOLID, filter, hull))
      return false;
    *z = tr.endpos.z;
    *nz = tr.plane.normal.z;
    return true;
  };
  auto find = [&](int cx, int cy, float z) -> int {
    auto it = columns.find(colKey(cx, cy));
    if (it == columns.end()) return -1;
    for (uint32_t i : it->second)
      if (std::fabs(cells_[i].z - z) < kZSeparate) return (int)i;
    return -1;
  };
  auto add = [&](int cx, int cy, float z, float nz) {
    cells_.push_back(FloodCell{cx, cy, z, nz, 0, 0});
    columns[colKey(cx, cy)].push_back((uint32_t)cells_.size() - 1);
    return (uint32_t)cells_.size() - 1;
  };

  for (const Vector& s : seeds) {
    int cx = (int)std::floor(s.x / kCell), cy = (int)std::floor(s.y / kCell);
    float z, nz;
    if (!probe(cx, cy, s.z, &z, &nz)) continue;
    if (find(cx, cy, z) < 0) add(cx, cy, z, nz);
  }

  static const int DX[4] = {1, -1, 0, 0}, DY[4] = {0, 0, 1, -1};
  for (uint32_t i = 0; i < cells_.size(); ++i) {
    if (cells_.size() >= kMaxCells) {
      floodCapped_ = true;
      break;
    }
    FloodCell c = cells_[i];  // add() below reallocates cells_
    for (int d = 0; d < 4; ++d) {
      if (c.walkMask & 1 << d) continue;  // linked when the neighbor expanded
      int nx = c.cx + DX[d], ny = c.cy + DY[d];
      float z, nz;
      if (!probe(nx, ny, c.z, &z, &nz)) continue;
      float dz = z - c.z;
      float climb = c.nz > kFlatNz && nz > kFlatNz ? kStepClimb : kSlopeClimb;
      bool walk = std::fabs(dz) <= climb;
      if (!walk && (dz > 0 || -dz > kMaxDrop)) continue;
      float sz = std::max(c.z + hullLift(c.nz), z + hullLift(nz));
      Vector from{center(c.cx), center(c.cy), sz};
      Vector to{center(nx), center(ny), sz};
      CGameTrace sweep;
      if (engine->TraceHull(from, to, hmin, hmax, MASK_PLAYERSOLID, filter,
                            sweep))
        continue;
      int found = find(nx, ny, z);
      uint32_t ni = found >= 0 ? (uint32_t)found : add(nx, ny, z, nz);
      if (walk) {
        cells_[i].walkMask |= 1 << d;
        cells_[ni].walkMask |= 1 << (d ^ 1);
      } else {
        cells_[i].dropMask |= 1 << d;
      }
    }
  }
  floodMs_ = std::chrono::duration<float, std::milli>(
                 std::chrono::steady_clock::now() - t0)
                 .count();
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

  const std::vector<NavSkeleton::FloodCell>& cells = nav.Cells();
  int walkLinks = 0, dropLinks = 0;
  float zMin = 1e30f, zMax = -1e30f;
  for (const NavSkeleton::FloodCell& c : cells) {
    walkLinks += __builtin_popcount(c.walkMask);
    dropLinks += __builtin_popcount(c.dropMask);
    zMin = std::min(zMin, c.z);
    zMax = std::max(zMax, c.z);
  }
  if (cells.empty())
    console->Print("nav_dump: flood 0 cells (%.0f ms)\n", nav.FloodMs());
  else
    console->Print(
        "nav_dump: flood %d cells z[%.0f..%.0f], %d walk + %d drop links "
        "(%.0f ms)%s\n",
        (int)cells.size(), zMin, zMax, walkLinks / 2, dropLinks, nav.FloodMs(),
        nav.FloodCapped() ? "  CAPPED" : "");
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

// Time the engine trace kinds a floor flood issues: ray down-trace,
// zero-length hull fit, 32u swept hull. Origins are scattered over a 512u
// square around the player so the collision tree isn't cache-hot from
// repeating one segment (that would report an optimistic lower bound).
// Blocks the main thread for the duration.
CON_COMMAND(sar_harness_trace_bench,
            "sar_harness_trace_bench [n] - time n of each engine trace kind "
            "(ray down, hull fit, swept hull) scattered around the player. "
            "Blocks the main thread.\n") {
  ServerEnt* pl = server ? server->GetPlayer(1) : nullptr;
  if (!pl) {
    console->Print("trace_bench: no player.\n");
    return;
  }
  long nArg = args.ArgC() > 1 ? std::strtol(args[1], nullptr, 10) : 10000;
  int n = (int)std::max(1L, std::min(nArg, 100000L));

  CTraceFilterSimple filter;
  filter.SetPassEntity(pl);
  Vector feet = pl->abs_origin();
  Vector hmin = pl->collision().OBBMins();
  Vector hmax = pl->collision().OBBMaxs();
  QAngle down{90, 0, 0};
  CGameTrace tr;
  auto jx = [](int i) { return float((i & 63) - 32) * 8.0f; };
  auto jy = [](int i) { return float(((i >> 6) & 63) - 32) * 8.0f; };

  console->Print("trace_bench: n=%d scattered +-256u around %.0f %.0f %.0f\n",
                 n, feet.x, feet.y, feet.z);
  auto bench = [&](const char* name, auto&& fn) {
    int hit = 0, solid = 0;
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < n; ++i) {
      if (fn(i)) hit++;
      if (tr.startsolid) solid++;
    }
    auto t1 = std::chrono::steady_clock::now();
    double us = std::chrono::duration<double, std::micro>(t1 - t0).count() / n;
    console->Print(
        "    %-10s %8.2f us/trace  (100k = %.0f ms)  hit %d/%d startsolid "
        "%d\n",
        name, us, us * 100.0, hit, n, solid);
  };
  bench("ray down", [&](int i) {
    Vector top{feet.x + jx(i), feet.y + jy(i), feet.z + 40.0f};
    return engine->Trace(top, down, 200.0f, MASK_PLAYERSOLID, filter, tr);
  });
  bench("hull fit", [&](int i) {
    Vector at{feet.x + jx(i), feet.y + jy(i), feet.z + 2.0f};
    return engine->TraceHull(at, at, hmin, hmax, MASK_PLAYERSOLID, filter, tr);
  });
  bench("hull sweep", [&](int i) {
    Vector at{feet.x + jx(i), feet.y + jy(i), feet.z + 2.0f};
    Vector fwd{at.x + 32.0f, at.y, at.z};
    return engine->TraceHull(at, fwd, hmin, hmax, MASK_PLAYERSOLID, filter, tr);
  });
}

// What is the player standing on? Down-trace at the feet, print the hit
// entity, material, plane normal, contents. Sees invisible collision
// (playerclip, TOOLSINVISIBLE brush entities) that render geometry doesn't.
CON_COMMAND(sar_harness_trace_down,
            "sar_harness_trace_down [dist] - down-trace at the player's feet "
            "(MASK_PLAYERSOLID); print hit entity, material, normal, "
            "contents.\n") {
  ServerEnt* pl = server ? server->GetPlayer(1) : nullptr;
  if (!pl) {
    console->Print("trace_down: no player.\n");
    return;
  }
  float dist = args.ArgC() > 1 ? (float)std::atof(args[1]) : 200.0f;
  CTraceFilterSimple filter;
  filter.SetPassEntity(pl);
  Vector feet = pl->abs_origin();
  Vector top{feet.x, feet.y, feet.z + 40.0f};
  QAngle down{90, 0, 0};
  CGameTrace tr;
  if (!engine->Trace(top, down, dist + 40.0f, MASK_PLAYERSOLID, filter, tr)) {
    console->Print("trace_down: no hit within %.0fu below the feet\n", dist);
    return;
  }
  const char* cls = tr.m_pEnt ? server->GetEntityClassName(tr.m_pEnt) : nullptr;
  const char* name = tr.m_pEnt ? server->GetEntityName(tr.m_pEnt) : nullptr;
  console->Print(
      "trace_down: hit z=%.1f (feet dz=%.1f)\n"
      "    normal   %.3f %.3f %.3f\n"
      "    entity   %s \"%s\"\n"
      "    material %s  contents 0x%x%s\n",
      tr.endpos.z, feet.z - tr.endpos.z, tr.plane.normal.x, tr.plane.normal.y,
      tr.plane.normal.z, cls && *cls ? cls : "(world)", name ? name : "",
      tr.surface.name ? tr.surface.name : "?", (unsigned)tr.contents,
      tr.startsolid ? "  STARTSOLID" : "");
}
