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
// Flood lattice. Climb between neighbor cells is capped at engine step height
// on flat ground, but at a full slope-rise when either cell is inclined (a
// walkable-limit 45.57deg slope rises ~33u per 32u cell; a 27.9deg stair ramp
// ~17u). Distinct stacked floors in one column are >=72u apart (hull height),
// so kZSeparate splits them unambiguously.
constexpr float kCell = NavSkeleton::kCellSize;
constexpr float kLift = 2.0f;
constexpr float kWalkFlat = 8.0f;  // |dz| <= this: same level
constexpr float kStepClimb = 18.0f;
constexpr float kSlopeClimb = 34.0f;
constexpr float kMaxDrop = 128.0f;
constexpr float kFlatNz = 0.95f;
constexpr float kZSeparate = 36.0f;
constexpr uint32_t kMaxCells = 100000;
constexpr uint32_t kMinLevel = 8;  // smaller clusters chain into connector runs

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
}  // namespace

void NavSkeleton::Build(const std::string& mapName) {
  gates_.clear();
  ServerEnt* pl = server ? server->GetPlayer(1) : nullptr;
  std::vector<Vector> seeds;
  if (pl) seeds.push_back(pl->abs_origin());
  for (const FloorSurface& fs : EnumerateFloorSurfaces(mapName))
    seeds.push_back(Vector{(fs.mins.x + fs.maxs.x) * 0.5f,
                           (fs.mins.y + fs.maxs.y) * 0.5f, fs.z});
  Flood(seeds);
  // TODO: deploy-state gates on cluster edges.
}

// Seeded BFS over the lattice: a cell exists iff the standing player hull
// rests there (down-ray finds a walkable-slope floor, zero-length hull fits
// above it), an edge iff a swept hull passes between neighbor centers at the
// higher floor. The trace is the walkability authority; BSP floor faces only
// seed it. Movers are traced at their live pose. Main thread.
void NavSkeleton::Flood(const std::vector<Vector>& seeds) {
  cells_.clear();
  clusters_.clear();
  clusterEdges_.clear();
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
        cells_[i].nbr[d] = ni;
        cells_[ni].walkMask |= 1 << (d ^ 1);
        cells_[ni].nbr[d ^ 1] = i;
      } else {
        cells_[i].dropMask |= 1 << d;
        cells_[i].nbr[d] = ni;
      }
    }
  }
  Cluster();
  floodMs_ = std::chrono::duration<float, std::milli>(
                 std::chrono::steady_clock::now() - t0)
                 .count();
}

// Collapse cells into the legible graph: flat walk links (|dz| <= kWalkFlat)
// union into level clusters, then crossing links whose pass-1 clusters are
// BOTH small union transitively — a staircase of treads or a chain of ramp
// cells becomes one connector run, but runs never absorb into a level.
// Smallness is judged on pre-merge sizes by design. Crossing links become
// typed directed cluster edges, deduped per (from, to, type).
void NavSkeleton::Cluster() {
  clusters_.clear();
  clusterEdges_.clear();
  if (cells_.empty()) return;

  std::vector<uint32_t> parent(cells_.size());
  for (uint32_t i = 0; i < parent.size(); ++i) parent[i] = i;
  auto root = [&](uint32_t v) {
    while (parent[v] != v) v = parent[v] = parent[parent[v]];
    return v;
  };
  auto join = [&](uint32_t a, uint32_t b) { parent[root(a)] = root(b); };

  for (uint32_t i = 0; i < cells_.size(); ++i)
    for (int d = 0; d < 4; ++d)
      if (cells_[i].walkMask & 1 << d) {
        uint32_t j = cells_[i].nbr[d];
        if (std::fabs(cells_[j].z - cells_[i].z) <= kWalkFlat) join(i, j);
      }

  std::vector<uint32_t> size(cells_.size(), 0);
  for (uint32_t i = 0; i < cells_.size(); ++i) size[root(i)]++;
  for (uint32_t i = 0; i < cells_.size(); ++i)
    for (int d = 0; d < 4; ++d)
      if (cells_[i].walkMask & 1 << d) {
        uint32_t ri = root(i), rj = root(cells_[i].nbr[d]);
        if (ri != rj && size[ri] < kMinLevel && size[rj] < kMinLevel)
          join(ri, rj);
      }

  std::unordered_map<uint32_t, uint32_t> idOf;
  for (uint32_t i = 0; i < cells_.size(); ++i) {
    FloodCell& c = cells_[i];
    float x0 = c.cx * kCell, y0 = c.cy * kCell;
    uint32_t r = root(i);
    auto it = idOf.find(r);
    if (it == idOf.end()) {
      it = idOf.emplace(r, (uint32_t)clusters_.size()).first;
      clusters_.push_back(CellCluster{(uint32_t)clusters_.size(), 0, c.z, c.z,
                                      Vector{x0, y0, c.z},
                                      Vector{x0 + kCell, y0 + kCell, c.z}});
    }
    CellCluster& cl = clusters_[it->second];
    cl.cells++;
    cl.zMin = std::min(cl.zMin, c.z);
    cl.zMax = std::max(cl.zMax, c.z);
    cl.mins.x = std::min(cl.mins.x, x0);
    cl.mins.y = std::min(cl.mins.y, y0);
    cl.mins.z = std::min(cl.mins.z, c.z);
    cl.maxs.x = std::max(cl.maxs.x, x0 + kCell);
    cl.maxs.y = std::max(cl.maxs.y, y0 + kCell);
    cl.maxs.z = std::max(cl.maxs.z, c.z);
    c.cluster = it->second;
  }

  std::unordered_map<uint64_t, bool> seen;
  auto emit = [&](uint32_t from, uint32_t to, uint8_t type, const Vector& via) {
    uint64_t key = ((uint64_t)from << 34) | ((uint64_t)to << 4) | type;
    if (seen.emplace(key, true).second)
      clusterEdges_.push_back(Edge{from, to, type, via, 0});
  };
  for (const FloodCell& c : cells_)
    for (int d = 0; d < 4; ++d) {
      if (!((c.walkMask | c.dropMask) & 1 << d)) continue;
      const FloodCell& n = cells_[c.nbr[d]];
      if (n.cluster == c.cluster) continue;
      float dz = n.z - c.z;
      uint8_t type = c.dropMask & 1 << d ? DROP
                     : std::fabs(dz) <= kWalkFlat
                         ? WALK
                         : (dz > 0 ? STEP_UP : STEP_DOWN);
      Vector via{(c.cx + n.cx + 1) * kCell * 0.5f,
                 (c.cy + n.cy + 1) * kCell * 0.5f, std::max(c.z, n.z)};
      emit(c.cluster, n.cluster, type, via);
    }
}

NavSkeleton::PlanResult NavSkeleton::Plan(const Vector& start,
                                          const Vector& target) {
  (void)start;
  PlanResult r;
  r.target = target;
  // TODO: global cluster A* + local within-cluster routing.
  return r;
}

// Build the nav flood for the current map and print cell, cluster and edge
// stats. Read-only.
CON_COMMAND(sar_harness_nav_dump,
            "sar_harness_nav_dump - build the go_to nav flood and print cell, "
            "cluster and edge stats.\n") {
  if (!engine) {
    console->Print("nav_dump: no engine.\n");
    return;
  }
  NavSkeleton nav;
  nav.Build(engine->GetCurrentMapName());

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

  const std::vector<NavSkeleton::CellCluster>& clusters = nav.Clusters();
  console->Print("nav_dump: %d clusters, %d cluster edges\n",
                 (int)clusters.size(), (int)nav.ClusterEdges().size());
  for (const NavSkeleton::CellCluster& cl : clusters)
    console->Msg("    C%u %u cells z[%.0f..%.0f] x[%.0f..%.0f] y[%.0f..%.0f]\n",
                 cl.id, cl.cells, cl.zMin, cl.zMax, cl.mins.x, cl.maxs.x,
                 cl.mins.y, cl.maxs.y);
  for (const NavSkeleton::Edge& e : nav.ClusterEdges())
    console->Msg("    C%u -%s-> C%u @ %.0f %.0f %.0f\n", e.from,
                 EdgeName(e.type), e.to, e.via.x, e.via.y, e.via.z);
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
