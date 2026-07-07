#pragma once

#include "PanelSource.hpp"

// Enumerates a chamber's portalable wall panels by parsing the map's .bsp from
// the engine search paths.
class BspFilePanelSource : public IPanelSource {
 public:
  std::vector<PanelDesc> EnumeratePanels(const std::string& mapName) override;
  // Brush-entity portalable surfaces at their compile-time rest pose, for
  // runtime posing against the live entities.
  std::vector<DynamicPanelRest> EnumerateDynamicRests(
      const std::string& mapName);
};

// Walkable floor surfaces (contiguous coplanar floor faces) parsed from the
// map's .bsp -- seeds for the go_to nav flood.
std::vector<FloorSurface> EnumerateFloorSurfaces(const std::string& mapName);

// One direct I/O wiring from the map's entity lump: src's "OnX" output fires
// target. Single hop -- relay/counter chains are not followed.
struct IoLink {
  std::string src;
  std::string srcClass;
  std::string target;
};
std::vector<IoLink> EnumerateIoLinks(const std::string& mapName);
