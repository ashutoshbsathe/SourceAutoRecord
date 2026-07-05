#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "Utils/SDK/Math.hpp"

// Multi-Z walkable-surface graph for go_to. Surfaces are parsed from the map
// .bsp; edges are height-banded transitions between them, some gated on a live
// mover pose. A plan is a global surface route refined by local within-surface
// routing, returned as steps the follower and the path visualizer both consume.
class NavSkeleton {
 public:
  enum EdgeType : uint8_t { WALK, STEP_UP, STEP_DOWN, DROP, PORTAL, FLING };
  enum PlanCode : uint8_t { SUCCESS, REACHED_PROJECTION, NO_ROUTE, STUCK };
  enum BlockReason : uint8_t { NONE, NO_FLOOR, IN_WALL, SEVERED, ABOVE_REACH };

  struct Surface {
    uint32_t id = 0;
    Vector normal{0, 0, 1};
    float z = 0;
    Vector mins, maxs;
    std::vector<Vector> poly;  // outer winding, world space
    uint32_t dynEnt = 0;  // 0 = static world; else the brush entity posing it
  };
  struct Edge {
    uint32_t from = 0, to = 0;
    uint8_t type = WALK;
    Vector via;         // transition point between the two surfaces
    uint32_t gate = 0;  // 0 = always-open; else index into gates_
  };
  struct Gate {
    uint32_t ctrlEnt = 0;  // mover whose live pose enables the edge
    float enableZ = 0;
    uint32_t button = 0;  // controlling button, surfaced as an agent hint
  };

  struct PlanStep {
    Vector pos;
    float floorZ = 0;
    uint8_t edgeType = WALK;
    uint32_t surface = 0;
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
  bool Ready() const { return !surfaces_.empty(); }
  const std::vector<Surface>& Surfaces() const { return surfaces_; }

 private:
  std::vector<Surface> surfaces_;
  std::vector<Edge> edges_;
  std::vector<Gate> gates_;
};
