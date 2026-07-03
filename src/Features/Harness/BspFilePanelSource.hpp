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
