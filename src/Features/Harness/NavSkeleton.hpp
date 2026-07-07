#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "Utils/SDK/Math.hpp"

// Multi-Z walkable-cell graph for go_to, built by a seeded hull-trace flood
// over the live map (BSP floor faces only seed it). Cells cluster into flat
// levels + connector runs (stairs, ramps) joined by typed edges, some gated
// on a live mover pose. A plan is a global cluster route refined locally,
// returned as steps the follower and the path visualizer both consume.
class NavSkeleton {
 public:
  static constexpr float kCellSize = 32.0f;  // flood lattice pitch

  enum EdgeType : uint8_t { WALK, STEP_UP, STEP_DOWN, DROP, PORTAL, FLING };
  enum PlanCode : uint8_t { SUCCESS, REACHED_PROJECTION, NO_ROUTE, STUCK };
  enum BlockReason : uint8_t { NONE, NO_FLOOR, IN_WALL, SEVERED, ABOVE_REACH };

  struct Edge {
    uint32_t from = 0, to = 0;  // cluster ids
    uint8_t type = WALK;
    Vector via;  // one crossing point, advisory: dedupe keeps an arbitrary
                 // one when several doorways join the same cluster pair
    uint32_t gate = 0;  // 0 = always-open; else 1-based index into gates_
  };
  struct Gate {
    std::string mover;   // targetname of the brush entity the crossing rests on
    float moverZ = 0;    // z at flood time: the pose this edge exists under
    std::string button;  // a button wired to the mover in the map's I/O
  };

  static constexpr uint32_t kNoCell = 0xFFFFFFFF;

  // One 32u lattice cell the trace flood proved standable. walkMask/dropMask
  // record which of the 4 neighbors (+x,-x,+y,-y) the body can reach: walk
  // links are bidirectional (set on both cells), drops are outgoing only.
  // nbr[d] resolves the linked cell's index for either link kind.
  struct FloodCell {
    int cx = 0, cy = 0;
    float z = 0;
    float nz = 1;  // floor normal z at the cell
    uint8_t walkMask = 0;
    uint8_t dropMask = 0;
    uint32_t nbr[4] = {kNoCell, kNoCell, kNoCell, kNoCell};
    uint32_t cluster = 0;
    uint32_t gate = 0;  // as Edge::gate, for the mover this cell rests on
  };

  // A maximal group of flood cells: a flat level (walk-linked at |dz| <=
  // walk-flat) or a chain of small clusters merged into one connector run
  // (stair treads, ramp cells).
  struct CellCluster {
    uint32_t id = 0;
    uint32_t cells = 0;
    float zMin = 0, zMax = 0;
    Vector mins, maxs;
  };

  struct PlanStep {
    Vector pos;
    float floorZ = 0;
    uint8_t edgeType = WALK;
    uint32_t cluster = 0;
  };
  struct PlanResult {
    std::vector<PlanStep> steps;  // goal last
    bool reached = false;
    Vector standPos;  // where the body ends up: the goal, or its projection
    Vector target;
    float residualDz = 0, residualDxy = 0;
    uint8_t code = NO_ROUTE;
    uint8_t blockReason = NONE;
  };

  void Build(const std::string& mapName);
  PlanResult Plan(const Vector& start, const Vector& target);
  bool Ready() const { return !cells_.empty(); }
  const std::vector<FloodCell>& Cells() const { return cells_; }
  const std::vector<CellCluster>& Clusters() const { return clusters_; }
  const std::vector<Edge>& ClusterEdges() const { return clusterEdges_; }
  const std::vector<Gate>& Gates() const { return gates_; }
  float FloodMs() const { return floodMs_; }
  bool FloodCapped() const { return floodCapped_; }

 private:
  void Flood(const std::vector<Vector>& seeds);
  void Cluster();  // cells -> level clusters + connector runs + typed edges

  std::vector<Gate> gates_;
  std::vector<FloodCell> cells_;
  std::vector<CellCluster> clusters_;
  std::vector<Edge> clusterEdges_;
  float floodMs_ = 0;
  bool floodCapped_ = false;
};
