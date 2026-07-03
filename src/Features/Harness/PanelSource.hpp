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

// Supplies a chamber's portalable panels. A sidecar backs this today; a runtime
// BSP/trace walk swaps in behind the same interface.
class IPanelSource {
 public:
  virtual ~IPanelSource() = default;
  virtual std::vector<PanelDesc> EnumeratePanels(
      const std::string& mapName) = 0;
};
