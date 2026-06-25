#pragma once
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "Utils/SDK/Math.hpp"

// Lazy occupancy grid + A* for go_to global routing. Cells are hull-probed
// on demand and cached -- only the explored frontier is touched, no reset-time
// sweep. Anchored to the world lattice at kCellSize; 2D connectivity (current
// chambers are ~flat) with floorZ stored per cell as the 2.5D-ready seam.
// Obstacle props (cubes/buttons/turrets) are stamped BLOCKED so the route bends
// around them, not just walls. Construct once per go_to from the live player;
// every method traces the world / reads the entity list and must run on the
// main thread.
class GoToPlanner {
 public:
  static constexpr float kCellSize = 16.0f;  // ~half player hull; 128/8 divisor

  enum CellState : uint8_t { UNKNOWN = 0, WALKABLE, BLOCKED };
  enum BlockReason : uint8_t { OPEN = 0, NO_FLOOR, IN_WALL, OBSTACLE };
  struct Cell {
    uint8_t state = UNKNOWN;
    uint8_t reason = OPEN;  // why BLOCKED; recon-only, A* never reads it
    float floorZ = 0;
  };

  // targetKey/heldKey: obstacle props skipped by identity (the go_to
  // destination and any carried cube), matching the VFH histogram's stance.
  GoToPlanner(const Vector& playerMins, const Vector& playerMaxs, float refZ,
              uint32_t targetKey = 0, uint32_t heldKey = 0);

  static int CellX(float x) {
    return static_cast<int>(std::floor(x / kCellSize));
  }
  static int CellY(float y) {
    return static_cast<int>(std::floor(y / kCellSize));
  }
  static float CenterX(int cx) { return (cx + 0.5f) * kCellSize; }
  static float CenterY(int cy) { return (cy + 0.5f) * kCellSize; }
  static uint32_t CellKey(int cx, int cy) {
    return (static_cast<uint32_t>(static_cast<uint16_t>(cx)) << 16) |
           static_cast<uint16_t>(cy);
  }

  const Cell& At(int cx, int cy);  // probe-or-cache; ref stable (node map)
  bool Passable(int ax, int ay, int bx, int by);  // hull-sweep + corner guard

  // Route from startPos to goalPos as cell-center waypoints (goal last, start
  // excluded). Empty if the goal is unreachable or the expansion cap is hit.
  std::vector<Vector> Plan(const Vector& startPos, const Vector& goalPos);

 private:
  struct ObstacleCircle {
    float x, y, r;
  };
  Cell Probe(int cx, int cy) const;
  bool SnapGoal(int* gx,
                int* gy);  // spiral to nearest walkable if goal blocked
  static int KeyX(uint32_t k) { return static_cast<int16_t>(k >> 16); }
  static int KeyY(uint32_t k) { return static_cast<int16_t>(k & 0xFFFF); }

  Vector mins_, maxs_;
  float refZ_;
  float halfWidth_;
  std::vector<ObstacleCircle> obstacles_;
  std::unordered_map<uint32_t, Cell> cells_;
};
