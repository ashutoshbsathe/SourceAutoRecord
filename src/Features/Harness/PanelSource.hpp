#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Utils/SDK/Math.hpp"

// One portalable wall panel: a cluster of coplanar 128u tiles. Panel marks are
// a namespace separate from entity marks. Sources number panels
// deterministically (sort by plane then min corner) so the offline sidecar and
// a runtime walk agree where the geometry agrees.
struct PanelDesc {
  int mark;
  Vector planeNormal;
  Vector center;
  Vector mins;
  Vector maxs;
  Vector corners[4];  // in-plane rect:
                      // (umin,vmin)(umax,vmin)(umax,vmax)(umin,vmax)
  int anchorFlags;
};

// (u,v) in [0,1]^2 -> world point on the panel rect; (0.5,0.5) is the center.
Vector ResolvePanelPoint(const PanelDesc& p, float u, float v);

// Deterministic in-plane basis for a plane normal, shared by enumeration and
// runtime posing so corner ordering agrees everywhere.
void PlaneAxes(Vector n, Vector* u, Vector* v);

// A brush-entity portalable surface (angled panel, flip panel, ...) at its
// compile-time rest pose. Brush models compile about the entity origin, so the
// corners are entity-local; the live entity's transform poses them in the
// world.
struct DynamicPanelRest {
  std::string targetname;
  Vector normal;      // local outward normal
  Vector corners[4];  // local in-plane rect:
                      // (umin,vmin)(umax,vmin)(umax,vmax)(umin,vmax)
};

// One walkable floor surface: a cluster of contiguous, coplanar floor faces
// (normal.z > ~0.7). z is the stand height; corners are the in-plane bounding
// rect, mins/maxs the world AABB.
struct FloorSurface {
  Vector normal;
  float z;
  Vector mins;
  Vector maxs;
  Vector corners[4];  // (umin,vmin)(umax,vmin)(umax,vmax)(umin,vmax)
};

// Supplies a chamber's portalable panels. A sidecar backs this today; a runtime
// BSP/trace walk swaps in behind the same interface.
class IPanelSource {
 public:
  virtual ~IPanelSource() = default;
  virtual std::vector<PanelDesc> EnumeratePanels(
      const std::string& mapName) = 0;
};
