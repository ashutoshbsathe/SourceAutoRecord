#include "GoToPlanner.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <queue>
#include <string>
#include <unordered_set>

#include "Command.hpp"
#include "Entity.hpp"
#include "Features/EntityList.hpp"
#include "Modules/Console.hpp"
#include "Modules/Engine.hpp"
#include "Modules/Server.hpp"
#include "Offsets.hpp"
#include "Utils/SDK/EntityEdict.hpp"
#include "Utils/SDK/Trace.hpp"

namespace {
// Floor search window around the player's feet: probe from a little above to
// well below, so the floor (and a modest step) is found.
constexpr float kProbeUp = 40.0f;
constexpr float kProbeDown = 128.0f;
constexpr float kHullLift = 2.0f;  // lift the body hull a hair off the floor
// A* tuning.
constexpr float kSqrt2 = 1.41421356f;       // diagonal step cost (cells)
constexpr float kHeuristicWeight = 1.001f;  // tie-break: trim frontier fan-out
constexpr int kPlanMaxCells = 400;  // expansion cap -> no path on overflow
constexpr int kSnapRadius = 4;      // goal spiral-snap search (cells)

// Props the route bends around.
bool IsObstacleClass(const char* cls) {
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
}  // namespace

GoToPlanner::GoToPlanner(const Vector& playerMins, const Vector& playerMaxs,
                         float refZ, uint32_t targetKey, uint32_t heldKey)
    : mins_(playerMins), maxs_(playerMaxs), refZ_(refZ) {
  halfWidth_ = std::max(maxs_.x, maxs_.y);
  // The target's own footprint (if an obstacle prop), so the stamp skips not
  // just the destination but anything overlapping it (the button a cube sits
  // on); else the route could never reach a cube-on-button.
  Vector tC{0, 0, 0};
  float tR = -1;
  if (targetKey) {
    CEntInfo* ti = entityList->GetEntityInfoByIndex(targetKey >> 16);
    if (ti && ti->m_pEntity &&
        static_cast<uint16_t>(ti->m_SerialNumber) ==
            static_cast<uint16_t>(targetKey) &&
        IsObstacleClass(server->GetEntityClassName(ti->m_pEntity))) {
      ICollideable& tc = SE(ti->m_pEntity)->collision();
      Vector mn = tc.OBBMins(), mx = tc.OBBMaxs();
      tC = SE(ti->m_pEntity)->abs_origin() + (mn + mx) * 0.5f;
      tR = 0.5f * Vector{mx.x - mn.x, mx.y - mn.y, 0}.Length2D();
    }
  }
  // Snapshot obstacle-prop footprints once (the grid stamp); skip the go_to
  // target, anything overlapping it, and any carried cube. Main thread.
  for (int i = 0; i < Offsets::NUM_ENT_ENTRIES; ++i) {
    CEntInfo* info = entityList->GetEntityInfoByIndex(i);
    if (!info || !info->m_pEntity) continue;
    uint32_t key = (static_cast<uint32_t>(i) << 16) |
                   static_cast<uint16_t>(info->m_SerialNumber);
    if (key == targetKey || key == heldKey) continue;
    if (!IsObstacleClass(server->GetEntityClassName(info->m_pEntity))) continue;
    ServerEnt* se = SE(info->m_pEntity);
    ICollideable& coll = se->collision();
    Vector mins = coll.OBBMins(), maxs = coll.OBBMaxs();
    Vector center = se->abs_origin() + (mins + maxs) * 0.5f;
    float footprintR =
        0.5f * Vector{maxs.x - mins.x, maxs.y - mins.y, 0}.Length2D();
    if (tR >= 0) {  // overlaps the target footprint -> skip (e.g. its button)
      float dxt = center.x - tC.x, dyt = center.y - tC.y;
      if (dxt * dxt + dyt * dyt < (tR + footprintR) * (tR + footprintR))
        continue;
    }
    obstacles_.push_back({center.x, center.y, footprintR + halfWidth_});
  }
}

const GoToPlanner::Cell& GoToPlanner::At(int cx, int cy) {
  uint32_t key = CellKey(cx, cy);
  auto it = cells_.find(key);
  if (it != cells_.end()) return it->second;
  return cells_.emplace(key, Probe(cx, cy)).first->second;
}

GoToPlanner::Cell GoToPlanner::Probe(int cx, int cy) const {
  Cell c;
  CTraceFilterSimple filter;
  filter.SetPassEntity(server->GetPlayer(1));
  float x = CenterX(cx), y = CenterY(cy);

  // 1. Floor: a straight-down point-ray from above refZ. No hit -> a pit / out
  // of bounds -> BLOCKED (floorZ stays 0, unused).
  Vector top{x, y, refZ_ + kProbeUp};
  QAngle down{90, 0, 0};
  CGameTrace floorTr;
  if (!engine->Trace(top, down, kProbeUp + kProbeDown, MASK_PLAYERSOLID, filter,
                     floorTr)) {
    c.state = BLOCKED;
    c.reason = NO_FLOOR;
    return c;
  }
  c.floorZ = floorTr.endpos.z;

  // 2. Does the body fit? A zero-length player-hull test just above the floor.
  // TraceHull reports startsolid/allsolid; a hull inside a wall otherwise
  // returns fraction 1 with a garbage normal, so use that to mark a wall cell
  // BLOCKED.
  Vector at{x, y, c.floorZ + kHullLift};
  CGameTrace hullTr;
  if (engine->TraceHull(at, at, mins_, maxs_, MASK_PLAYERSOLID, filter,
                        hullTr)) {
    c.state = BLOCKED;
    c.reason = IN_WALL;
    return c;
  }

  // 3. Obstacle stamp: a cell whose center is inside a cube/button footprint (+
  // body half-width) is BLOCKED, so the route bends around it.
  for (const ObstacleCircle& o : obstacles_) {
    float dx = x - o.x, dy = y - o.y;
    if (dx * dx + dy * dy < o.r * o.r) {
      c.state = BLOCKED;
      c.reason = OBSTACLE;
      return c;
    }
  }

  c.state = WALKABLE;
  return c;
}

bool GoToPlanner::Passable(int ax, int ay, int bx, int by) {
  // Only the destination is gated, never the source, so A* can leave a start
  // cell that probes BLOCKED (you're standing on it).
  if (At(bx, by).state != WALKABLE) return false;
  if (ax != bx &&
      ay != by) {  // diagonal: no corner-cut through a blocked corner
    if (At(ax, by).state != WALKABLE || At(bx, ay).state != WALKABLE)
      return false;
  }
  // Hull-sweep between centers: a wall lip refuses the body even when both ends
  // probe walkable.
  const Cell& ca = At(ax, ay);
  const Cell& cb = At(bx, by);
  float z = std::max(ca.floorZ, cb.floorZ) + kHullLift;
  Vector from{CenterX(ax), CenterY(ay), z};
  Vector to{CenterX(bx), CenterY(by), z};
  CTraceFilterSimple filter;
  filter.SetPassEntity(server->GetPlayer(1));
  CGameTrace tr;
  return !engine->TraceHull(from, to, mins_, maxs_, MASK_PLAYERSOLID, filter,
                            tr);
}

bool GoToPlanner::SnapGoal(int* gx, int* gy) {
  for (int r = 1; r <= kSnapRadius; ++r)
    for (int dy = -r; dy <= r; ++dy)
      for (int dx = -r; dx <= r; ++dx) {
        if (std::max(std::abs(dx), std::abs(dy)) != r) continue;  // ring only
        if (At(*gx + dx, *gy + dy).state == WALKABLE) {
          *gx += dx;
          *gy += dy;
          return true;
        }
      }
  return false;
}

std::vector<Vector> GoToPlanner::Plan(const Vector& startPos,
                                      const Vector& goalPos) {
  int sx = CellX(startPos.x), sy = CellY(startPos.y);
  int gx = CellX(goalPos.x), gy = CellY(goalPos.y);
  // Goal in a solid prop/wall (e.g. on a cube) -> snap to the nearest walkable
  // neighbour; the final approach leg closes the remaining gap.
  if (At(gx, gy).state != WALKABLE && !SnapGoal(&gx, &gy)) return {};

  auto octile = [](int dx, int dy) {
    int lo = std::min(dx, dy), hi = std::max(dx, dy);
    return (hi - lo) + kSqrt2 * lo;
  };

  struct Node {
    float f;
    int cx, cy;
  };
  auto worse = [](const Node& a, const Node& b) {
    return a.f > b.f;
  };  // min-heap
  std::priority_queue<Node, std::vector<Node>, decltype(worse)> open(worse);
  std::unordered_map<uint32_t, float> g;
  std::unordered_map<uint32_t, uint32_t> from;
  std::unordered_set<uint32_t> closed;

  uint32_t startKey = CellKey(sx, sy), goalKey = CellKey(gx, gy);
  g[startKey] = 0;
  open.push({octile(std::abs(sx - gx), std::abs(sy - gy)) * kHeuristicWeight,
             sx, sy});

  static const int dxs[8] = {1, -1, 0, 0, 1, 1, -1, -1};
  static const int dys[8] = {0, 0, 1, -1, 1, -1, 1, -1};
  bool reached = false;
  int expanded = 0;
  while (!open.empty()) {
    Node cur = open.top();
    open.pop();
    uint32_t curKey = CellKey(cur.cx, cur.cy);
    if (!closed.insert(curKey).second) continue;  // stale heap duplicate
    if (curKey == goalKey) {
      reached = true;
      break;
    }
    if (++expanded > kPlanMaxCells) return {};  // cap hit -> no usable path

    float curG = g[curKey];
    for (int k = 0; k < 8; ++k) {
      int nx = cur.cx + dxs[k], ny = cur.cy + dys[k];
      uint32_t nKey = CellKey(nx, ny);
      if (closed.count(nKey)) continue;
      if (!Passable(cur.cx, cur.cy, nx, ny)) continue;
      float tentative = curG + (k < 4 ? 1.0f : kSqrt2);
      auto it = g.find(nKey);
      if (it == g.end() || tentative < it->second) {
        g[nKey] = tentative;
        from[nKey] = curKey;
        float h = octile(std::abs(nx - gx), std::abs(ny - gy));
        open.push({tentative + h * kHeuristicWeight, nx, ny});
      }
    }
  }
  if (!reached) return {};

  // Reconstruct goal -> start, drop the start (you're already there), reverse.
  std::vector<Vector> path;
  for (uint32_t k = goalKey; k != startKey;) {
    int cx = KeyX(k), cy = KeyY(k);
    path.push_back({CenterX(cx), CenterY(cy), At(cx, cy).floorZ});
    auto it = from.find(k);
    if (it == from.end()) return {};  // broken chain (shouldn't happen)
    k = it->second;
  }
  std::reverse(path.begin(), path.end());
  return path;
}

// Debug: hull-probe a square of cells around the player and print the occupancy
// map (. walkable, # blocked/obstacle, @ player). Eyeball it against the
// visible floor/walls/cubes to validate the planner's cell math. The map is
// WORLD- absolute (+y/north at top), not view-relative.
CON_COMMAND(sar_harness_probe_cells,
            "sar_harness_probe_cells [radius] - hull-probe a grid of cells "
            "around the player and print the occupancy map (go_to "
            "planner). Default radius 6 cells.\n") {
  ServerEnt* pl = server ? server->GetPlayer(1) : nullptr;
  if (!pl) {
    console->Print("probe_cells: no player (load a map first).\n");
    return;
  }
  int radius = args.ArgC() > 1 ? std::atoi(args[1]) : 6;
  if (radius < 1) radius = 1;
  if (radius > 32) radius = 32;

  Vector feet = pl->abs_origin();
  ICollideable& coll = pl->collision();
  GoToPlanner planner(coll.OBBMins(), coll.OBBMaxs(), feet.z);
  int pcx = GoToPlanner::CellX(feet.x), pcy = GoToPlanner::CellY(feet.y);

  console->Print(
      "probe_cells: %dx%d cells @ %.0fu, player feet z=%.0f (^=+y)\n",
      2 * radius + 1, 2 * radius + 1, GoToPlanner::kCellSize, feet.z);
  for (int dy = radius; dy >= -radius; --dy) {  // +y (north) at top
    std::string row;
    for (int dx = -radius; dx <= radius; ++dx) {
      const GoToPlanner::Cell& c = planner.At(pcx + dx, pcy + dy);
      row += (dx == 0 && dy == 0)               ? '@'
             : c.state == GoToPlanner::WALKABLE ? '.'
                                                : '#';
    }
    console->Print("%s\n", row.c_str());
  }
}

// Debug: per-cell block reason (down-trace miss / hull-startsolid / obstacle
// stamp), whether a walkable cell is A*-reachable from the player's feet [A],
// and a floorZ-delta map [B]. A shallow gap (goo moat, lower walkway) can read
// walkable+reachable in [A] yet show a floor drop in [B]. Eyeball both against
// the visible chamber.
CON_COMMAND(sar_harness_laser_reachability_test,
            "sar_harness_laser_reachability_test [radius] - print the go_to "
            "planner's per-cell block reason, reachability-from-feet, and "
            "floorZ delta around the player. Default radius 6 cells.\n") {
  ServerEnt* pl = server ? server->GetPlayer(1) : nullptr;
  if (!pl) {
    console->Print("reachability_test: no player (load a map first).\n");
    return;
  }
  int radius = args.ArgC() > 1 ? std::atoi(args[1]) : 6;
  if (radius < 1) radius = 1;
  if (radius > 32) radius = 32;

  Vector feet = pl->abs_origin();
  ICollideable& coll = pl->collision();
  GoToPlanner planner(coll.OBBMins(), coll.OBBMaxs(), feet.z);
  int pcx = GoToPlanner::CellX(feet.x), pcy = GoToPlanner::CellY(feet.y);

  // BFS the reachable set from the player cell, bounded to the window. Passable
  // gates only the destination, so the start seeds even if it probes BLOCKED
  // (you're standing on it).
  static const int dxs[8] = {1, -1, 0, 0, 1, 1, -1, -1};
  static const int dys[8] = {0, 0, 1, -1, 1, -1, 1, -1};
  struct QCell {
    int cx, cy;
  };
  std::unordered_set<uint32_t> reachable;
  std::queue<QCell> bfs;
  reachable.insert(GoToPlanner::CellKey(pcx, pcy));
  bfs.push({pcx, pcy});
  while (!bfs.empty()) {
    QCell q = bfs.front();
    bfs.pop();
    for (int k = 0; k < 8; ++k) {
      int nx = q.cx + dxs[k], ny = q.cy + dys[k];
      if (std::abs(nx - pcx) > radius || std::abs(ny - pcy) > radius) continue;
      uint32_t nKey = GoToPlanner::CellKey(nx, ny);
      if (reachable.count(nKey)) continue;
      if (!planner.Passable(q.cx, q.cy, nx, ny)) continue;
      reachable.insert(nKey);
      bfs.push({nx, ny});
    }
  }

  console->Print("reachability_test: %dx%d cells @ %.0fu, feet z=%.0f (^=+y)\n",
                 2 * radius + 1, 2 * radius + 1, GoToPlanner::kCellSize,
                 feet.z);
  console->Print(
      "[A] @ you  . reachable  x severed  _ pit  # wall  O obstacle\n");
  for (int dy = radius; dy >= -radius; --dy) {
    std::string row;
    for (int dx = -radius; dx <= radius; ++dx) {
      int cx = pcx + dx, cy = pcy + dy;
      const GoToPlanner::Cell& c = planner.At(cx, cy);
      char ch;
      if (dx == 0 && dy == 0)
        ch = '@';
      else if (c.state == GoToPlanner::WALKABLE)
        ch = reachable.count(GoToPlanner::CellKey(cx, cy)) ? '.' : 'x';
      else if (c.reason == GoToPlanner::NO_FLOOR)
        ch = '_';
      else if (c.reason == GoToPlanner::IN_WALL)
        ch = '#';
      else
        ch = 'O';
      row += ch;
    }
    console->Print("%s\n", row.c_str());
  }

  console->Print(
      "[B] floorZ vs feet:  . level  u/U up  d/D down  (sp) no floor\n");
  for (int dy = radius; dy >= -radius; --dy) {
    std::string row;
    for (int dx = -radius; dx <= radius; ++dx) {
      int cx = pcx + dx, cy = pcy + dy;
      const GoToPlanner::Cell& c = planner.At(cx, cy);
      char ch;
      if (dx == 0 && dy == 0) {
        ch = '@';
      } else if (c.state == GoToPlanner::BLOCKED &&
                 c.reason == GoToPlanner::NO_FLOOR) {
        ch = ' ';
      } else {
        float d = c.floorZ - feet.z;
        if (d > -8.0f && d < 8.0f)
          ch = '.';
        else if (d >= 8.0f)
          ch = (d < 32.0f) ? 'u' : 'U';
        else
          ch = (d > -32.0f) ? 'd' : 'D';
      }
      row += ch;
    }
    console->Print("%s\n", row.c_str());
  }
}
