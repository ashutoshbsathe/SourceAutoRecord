#include "NavSkeleton.hpp"

void NavSkeleton::Build(const std::string& mapName) {
  (void)mapName;
  surfaces_.clear();
  edges_.clear();
  gates_.clear();
  // TODO: enumerate floor surfaces + banded/gated edges from the map .bsp.
}

NavSkeleton::PlanResult NavSkeleton::Plan(const Vector& start,
                                          const Vector& target) {
  (void)start;
  PlanResult r;
  r.target = target;
  // TODO: global surface A* + local within-surface routing.
  return r;
}
