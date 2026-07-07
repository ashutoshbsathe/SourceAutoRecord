#include "NavSkeleton.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <queue>
#include <unordered_map>

#include "BspFilePanelSource.hpp"
#include "Command.hpp"
#include "Entity.hpp"
#include "Features/EntityList.hpp"
#include "MarkTable.hpp"
#include "Modules/Console.hpp"
#include "Modules/Engine.hpp"
#include "Modules/Server.hpp"
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
constexpr int kRunSpan = 2;  // clusters at most this many cells across their
                             // narrow axis chain into connector runs

// Route costs (units of path length) and the goal's actionable envelope: a
// stand cell counts as arrival when the target is within arm's reach of the
// body standing there.
constexpr float kDropExtra = 64.0f;  // prefer stairs over a comparable ledge
constexpr float kReachXy = 64.0f;
constexpr float kReachUp = 88.0f;    // target at most this far above the floor
constexpr float kReachDown = 16.0f;  // ... or this far below it
constexpr float kInf = 1e30f;

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

const char* PlanName(uint8_t c) {
  switch (c) {
    case NavSkeleton::SUCCESS:
      return "SUCCESS";
    case NavSkeleton::REACHED_PROJECTION:
      return "REACHED_PROJECTION";
    case NavSkeleton::NO_ROUTE:
      return "NO_ROUTE";
    default:
      return "STUCK";
  }
}

const char* BlockName(uint8_t b) {
  switch (b) {
    case NavSkeleton::NO_FLOOR:
      return "NO_FLOOR";
    case NavSkeleton::IN_WALL:
      return "IN_WALL";
    case NavSkeleton::SEVERED:
      return "SEVERED";
    case NavSkeleton::ABOVE_REACH:
      return "ABOVE_REACH";
    case NavSkeleton::BELOW_REACH:
      return "BELOW_REACH";
    default:
      return "NONE";
  }
}
}  // namespace

uint64_t NavSkeleton::ColKey(int cx, int cy) {
  return (uint64_t)(uint32_t)cx << 32 | (uint32_t)cy;
}

uint32_t NavSkeleton::CellAt(int cx, int cy, float z) const {
  auto it = columns_.find(ColKey(cx, cy));
  if (it == columns_.end()) return kNoCell;
  for (uint32_t i : it->second)
    if (std::fabs(cells_[i].z - z) < kZSeparate) return i;
  return kNoCell;
}

void NavSkeleton::Build(const std::string& mapName) {
  gates_.clear();
  ServerEnt* pl = server ? server->GetPlayer(1) : nullptr;
  std::vector<Vector> seeds;
  if (pl) {  // Flood needs the player's hull; don't parse the .bsp without one
    seeds.push_back(pl->abs_origin());
    for (const FloorSurface& fs : EnumerateFloorSurfaces(mapName))
      seeds.push_back(Vector{(fs.mins.x + fs.maxs.x) * 0.5f,
                             (fs.mins.y + fs.maxs.y) * 0.5f, fs.z});
  }
  Flood(seeds);
  // Direct button->mover wirings annotate the gates; button->relay->mover
  // chains stay unresolved here (transitive inference is the sidecar's job).
  if (!gates_.empty())
    for (const IoLink& l : EnumerateIoLinks(mapName))
      if (l.srcClass.find("button") != std::string::npos)
        for (Gate& g : gates_)
          if (g.button.empty() && g.mover == l.target) g.button = l.src;
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
  columns_.clear();
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

  auto center = [](int c) { return c * kCell + kCell * 0.5f; };

  // A box on a slope rests on its uphill corner, above the center-ray hit by
  // up to half-width * slope gradient; the fit and the link sweep must sit
  // that high or any incline past ~7deg reads startsolid.
  float halfW = (hmax.x - hmin.x) * 0.5f;
  auto hullLift = [halfW](float nz) {
    return kLift + halfW * std::sqrt(2.0f * (1.0f - nz * nz)) / nz;
  };

  // Cells resting on a named brush entity (stairs, lifts, flip panels) carry
  // a gate: the flood already traces the mover's live pose, so the gate is
  // annotation (which mover, which button), not a plan-time filter. Named
  // movable props are not map machinery and stay gate-free.
  std::unordered_map<std::string, uint32_t> gateOf;
  auto moverGate = [&](void* ent) -> uint32_t {
    if (!ent) return 0;
    const char* nm = server->GetEntityName(ent);
    if (!nm || !*nm) return 0;
    const char* mdl = SE(ent)->field<char*>("m_ModelName");
    if (!mdl || mdl[0] != '*') return 0;
    auto it = gateOf.find(nm);
    if (it == gateOf.end()) {
      gates_.push_back(Gate{nm, SE(ent)->abs_origin().z, {}});
      it = gateOf.emplace(nm, (uint32_t)gates_.size()).first;
    }
    return it->second;
  };

  auto probe = [&](int cx, int cy, float fromZ, float* z, float* nz,
                   uint32_t* gate) {
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
    *gate = moverGate(tr.m_pEnt);
    return true;
  };
  auto add = [&](int cx, int cy, float z, float nz, uint32_t gate) {
    FloodCell fc{cx, cy, z, nz};
    fc.gate = gate;
    cells_.push_back(fc);
    columns_[ColKey(cx, cy)].push_back((uint32_t)cells_.size() - 1);
    return (uint32_t)cells_.size() - 1;
  };

  for (const Vector& s : seeds) {
    int cx = (int)std::floor(s.x / kCell), cy = (int)std::floor(s.y / kCell);
    float z, nz;
    uint32_t g;
    if (!probe(cx, cy, s.z, &z, &nz, &g)) continue;
    if (CellAt(cx, cy, z) == kNoCell) add(cx, cy, z, nz, g);
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
      uint32_t g;
      if (!probe(nx, ny, c.z, &z, &nz, &g)) continue;
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
      uint32_t found = CellAt(nx, ny, z);
      uint32_t ni = found != kNoCell ? found : add(nx, ny, z, nz, g);
      if (walk) {
        // A second link into ni's slot needs two floors of one column within
        // 2*kSlopeClimb (68u) of ni; standable floors sit >= 72u apart.
        assert(cells_[ni].nbr[d ^ 1] == kNoCell);
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
// BOTH narrow union transitively — a staircase of treads or a chain of ramp
// cells becomes one connector run, but runs never absorb into a level.
// Narrowness is the cluster's narrow-axis span (a tread stays narrow however
// wide the stair), judged pre-merge by design. Crossing links become typed
// directed cluster edges, deduped per (from, to, type); the first crossing's
// via and gate win (both advisory). An edge is gated iff either crossing
// cell rests on a mover.
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

  std::vector<int> x0(cells_.size(), INT_MAX), x1(cells_.size(), INT_MIN);
  std::vector<int> y0(cells_.size(), INT_MAX), y1(cells_.size(), INT_MIN);
  for (uint32_t i = 0; i < cells_.size(); ++i) {
    uint32_t r = root(i);
    x0[r] = std::min(x0[r], cells_[i].cx);
    x1[r] = std::max(x1[r], cells_[i].cx);
    y0[r] = std::min(y0[r], cells_[i].cy);
    y1[r] = std::max(y1[r], cells_[i].cy);
  }
  auto narrow = [&](uint32_t r) {
    return std::min(x1[r] - x0[r], y1[r] - y0[r]) < kRunSpan;
  };
  for (uint32_t i = 0; i < cells_.size(); ++i)
    for (int d = 0; d < 4; ++d)
      if (cells_[i].walkMask & 1 << d) {
        uint32_t ri = root(i), rj = root(cells_[i].nbr[d]);
        if (ri != rj && narrow(ri) && narrow(rj)) join(ri, rj);
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
  auto emit = [&](uint32_t from, uint32_t to, uint8_t type, const Vector& via,
                  uint32_t gate) {
    uint64_t key = ((uint64_t)from << 34) | ((uint64_t)to << 4) | type;
    if (seen.emplace(key, true).second)
      clusterEdges_.push_back(Edge{from, to, type, via, gate});
  };
  for (const FloodCell& c : cells_)
    for (int d = 0; d < 4; ++d) {
      if (!((c.walkMask | c.dropMask) & 1 << d)) continue;
      const FloodCell& n = cells_[c.nbr[d]];
      if (n.cluster == c.cluster) continue;
      // walk links at |dz| <= kWalkFlat never cross clusters (pass 1 joins
      // them), so a crossing walk link is always a step
      float dz = n.z - c.z;
      uint8_t type = c.dropMask & 1 << d ? DROP : dz > 0 ? STEP_UP : STEP_DOWN;
      Vector via{(c.cx + n.cx + 1) * kCell * 0.5f,
                 (c.cy + n.cy + 1) * kCell * 0.5f, std::max(c.z, n.z)};
      emit(c.cluster, n.cluster, type, via, c.gate ? c.gate : n.gate);
    }
}

// Single-source shortest path over the whole flood (walk links symmetric,
// drops directed), then two-tier goal resolution: the cheapest reachable
// cell inside the target's actionable envelope wins (SUCCESS); with none,
// the reachable cell nearest the target is its projection and the plan
// walks there anyway (REACHED_PROJECTION + residuals). The cluster graph is
// the legible/annotation layer; routing runs on cells.
NavSkeleton::PlanResult NavSkeleton::Plan(const Vector& start,
                                          const Vector& target) const {
  PlanResult r;
  r.target = target;
  r.standPos = start;
  if (cells_.empty()) {
    r.blockReason = NO_FLOOR;
    return r;
  }

  auto centerOf = [this](uint32_t i) {
    const FloodCell& c = cells_[i];
    return Vector{(c.cx + 0.5f) * kCell, (c.cy + 0.5f) * kCell, c.z};
  };

  // start = the feet's own column when it holds a standable cell (grounded,
  // on a prop, or airborne over it); neighbor columns only back it up,
  // scored by 3D distance and never above the feet by more than a mountable
  // step (an adjacent ledge must not capture the start).
  int scx = (int)std::floor(start.x / kCell);
  int scy = (int)std::floor(start.y / kCell);
  uint32_t sc = kNoCell;
  float bestDz = kInf;
  if (auto it = columns_.find(ColKey(scx, scy)); it != columns_.end())
    for (uint32_t i : it->second) {
      float dz = start.z - cells_[i].z;  // positive: cell below the feet
      if (dz < -kSlopeClimb || dz > kMaxDrop) continue;
      if (std::fabs(dz) < bestDz) {
        bestDz = std::fabs(dz);
        sc = i;
      }
    }
  if (sc == kNoCell) {
    float best = kInf;
    for (int dy = -1; dy <= 1; ++dy)
      for (int dx = -1; dx <= 1; ++dx) {
        if (!dx && !dy) continue;
        auto it = columns_.find(ColKey(scx + dx, scy + dy));
        if (it == columns_.end()) continue;
        for (uint32_t i : it->second) {
          float dz = start.z - cells_[i].z;
          if (dz < -kStepClimb || dz > kMaxDrop) continue;
          Vector p = centerOf(i);
          float d = (p.x - start.x) * (p.x - start.x) +
                    (p.y - start.y) * (p.y - start.y) + dz * dz;
          if (d < best) {
            best = d;
            sc = i;
          }
        }
      }
  }
  if (sc == kNoCell) {
    r.blockReason = NO_FLOOR;
    return r;
  }

  std::vector<float> dist(cells_.size(), kInf);
  std::vector<uint32_t> prev(cells_.size(), kNoCell);
  using QE = std::pair<float, uint32_t>;
  std::priority_queue<QE, std::vector<QE>, std::greater<QE>> pq;
  dist[sc] = 0;
  pq.push({0, sc});
  while (!pq.empty()) {
    auto [d, i] = pq.top();
    pq.pop();
    if (d > dist[i]) continue;
    const FloodCell& c = cells_[i];
    for (int k = 0; k < 4; ++k) {
      if (!((c.walkMask | c.dropMask) & 1 << k)) continue;
      uint32_t j = c.nbr[k];
      float w = kCell + std::fabs(cells_[j].z - c.z) +
                (c.dropMask & 1 << k ? kDropExtra : 0.0f);
      if (d + w < dist[j]) {
        dist[j] = d + w;
        prev[j] = i;
        pq.push({dist[j], j});
      }
    }
  }

  uint32_t goal = kNoCell;
  bool actionable = false;  // does any actionable cell exist, reachable or not
  float bestCost = kInf;
  for (uint32_t i = 0; i < cells_.size(); ++i) {
    Vector p = centerOf(i);
    float dx = p.x - target.x, dy = p.y - target.y, dz = target.z - p.z;
    if (dx * dx + dy * dy > kReachXy * kReachXy || dz < -kReachDown ||
        dz > kReachUp)
      continue;
    actionable = true;
    if (dist[i] < bestCost) {
      bestCost = dist[i];
      goal = i;
    }
  }
  if (goal != kNoCell) {
    r.code = SUCCESS;
    r.reached = true;
  } else {
    float best = kInf;
    for (uint32_t i = 0; i < cells_.size(); ++i) {
      if (dist[i] >= kInf) continue;
      Vector p = centerOf(i);
      float dx = p.x - target.x, dy = p.y - target.y, dz = p.z - target.z;
      float d3 = dx * dx + dy * dy + dz * dz;
      if (d3 < best) {
        best = d3;
        goal = i;
      }
    }
    r.code = REACHED_PROJECTION;
    // Every projection carries a reason; NO_FLOOR here means the target has
    // no stand cell within lateral reach anywhere (hovering over a gap).
    float dz = target.z - cells_[goal].z;
    r.blockReason = actionable         ? SEVERED
                    : dz > kReachUp    ? ABOVE_REACH
                    : dz < -kReachDown ? BELOW_REACH
                                       : NO_FLOOR;
  }

  Vector g = centerOf(goal);
  r.standPos = g;
  r.residualDxy = std::sqrt((target.x - g.x) * (target.x - g.x) +
                            (target.y - g.y) * (target.y - g.y));
  r.residualDz = target.z - g.z;

  std::vector<uint32_t> chain;
  for (uint32_t i = goal; i != kNoCell; i = prev[i]) chain.push_back(i);
  std::reverse(chain.begin(), chain.end());

  auto linkType = [this](uint32_t a, uint32_t b) -> uint8_t {
    const FloodCell& ca = cells_[a];
    for (int k = 0; k < 4; ++k) {
      if (ca.nbr[k] != b) continue;
      if (ca.dropMask & 1 << k) return DROP;
      float dz = cells_[b].z - ca.z;
      return std::fabs(dz) <= kWalkFlat ? WALK : dz > 0 ? STEP_UP : STEP_DOWN;
    }
    return WALK;
  };
  auto emit = [&](uint32_t i, uint8_t type) {
    r.steps.push_back(
        PlanStep{centerOf(i), cells_[i].z, type, cells_[i].cluster});
  };
  emit(chain[0], WALK);
  // a step per heading or travel-type change; straight same-type runs skip
  for (size_t k = 1; k < chain.size(); ++k) {
    uint8_t t = linkType(chain[k - 1], chain[k]);
    if (k + 1 < chain.size() && linkType(chain[k], chain[k + 1]) == t &&
        cells_[chain[k + 1]].cx - cells_[chain[k]].cx ==
            cells_[chain[k]].cx - cells_[chain[k - 1]].cx &&
        cells_[chain[k + 1]].cy - cells_[chain[k]].cy ==
            cells_[chain[k]].cy - cells_[chain[k - 1]].cy)
      continue;
    emit(chain[k], t);
  }
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
  console->Print("nav_dump: %d clusters, %d cluster edges, %d gates\n",
                 (int)clusters.size(), (int)nav.ClusterEdges().size(),
                 (int)nav.Gates().size());
  for (const NavSkeleton::CellCluster& cl : clusters)
    console->Msg("    C%u %u cells z[%.0f..%.0f] x[%.0f..%.0f] y[%.0f..%.0f]\n",
                 cl.id, cl.cells, cl.zMin, cl.zMax, cl.mins.x, cl.maxs.x,
                 cl.mins.y, cl.maxs.y);
  for (const NavSkeleton::Edge& e : nav.ClusterEdges()) {
    std::string gate;
    if (e.gate) {
      const NavSkeleton::Gate& g = nav.Gates()[e.gate - 1];
      gate =
          "  [" + g.mover + (g.button.empty() ? "" : " btn " + g.button) + "]";
    }
    console->Msg("    C%u -%s-> C%u @ %.0f %.0f %.0f%s\n", e.from,
                 EdgeName(e.type), e.to, e.via.x, e.via.y, e.via.z,
                 gate.c_str());
  }
}

// Flood the current map and plan a route from the player's feet to a point
// or a marked entity. Prints the plan and hands it to the nav visualizer as
// a ghost path. Read-only: nothing moves.
CON_COMMAND(sar_harness_nav_plan,
            "sar_harness_nav_plan <x y z | mark> - flood the map and plan a "
            "walk route from the player to the point (or marked entity); "
            "print the steps and store the ghost path for the nav draw. "
            "Read-only.\n") {
  ServerEnt* pl = server ? server->GetPlayer(1) : nullptr;
  if (!pl || !engine) {
    console->Print("nav_plan: no player.\n");
    return;
  }
  Vector target;
  if (args.ArgC() == 4) {
    target = Vector{(float)std::atof(args[1]), (float)std::atof(args[2]),
                    (float)std::atof(args[3])};
  } else if (args.ArgC() == 2) {
    auto [idx, ser] = markTable.GetEntityFromMark(std::atoi(args[1]));
    CEntInfo* info = idx >= 0 ? entityList->GetEntityInfoByIndex(idx) : nullptr;
    if (!info || !info->m_pEntity ||
        static_cast<uint16_t>(info->m_SerialNumber) != ser) {
      console->Print("nav_plan: bad mark %s.\n", args[1]);
      return;
    }
    target = SE(info->m_pEntity)->abs_origin();
  } else {
    console->Print("nav_plan: expected <x y z> or <mark>.\n");
    return;
  }

  NavSkeleton nav;
  nav.Build(engine->GetCurrentMapName());
  auto t0 = std::chrono::steady_clock::now();
  NavSkeleton::PlanResult r = nav.Plan(pl->abs_origin(), target);
  float planMs = std::chrono::duration<float, std::milli>(
                     std::chrono::steady_clock::now() - t0)
                     .count();
  NavGhostSet(r, engine->GetCurrentMapName());

  console->Print("nav_plan: %s%s%s  (flood %.0f ms, plan %.1f ms)\n",
                 PlanName(r.code), r.blockReason ? " / " : "",
                 r.blockReason ? BlockName(r.blockReason) : "", nav.FloodMs(),
                 planMs);
  console->Print(
      "    target %.0f %.0f %.0f  stand %.0f %.0f %.0f  residual dz %.0f "
      "dxy %.0f\n",
      target.x, target.y, target.z, r.standPos.x, r.standPos.y, r.standPos.z,
      r.residualDz, r.residualDxy);
  for (size_t i = 0; i < r.steps.size(); ++i) {
    const NavSkeleton::PlanStep& s = r.steps[i];
    console->Msg("    %2d. %-4s to %.0f %.0f %.0f (C%u)\n", (int)i,
                 EdgeName(s.edgeType), s.pos.x, s.pos.y, s.pos.z, s.cluster);
  }
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
