#pragma once

#include "PanelSource.hpp"

// Loads a chamber's panels from an offline sidecar at
// sar_harness_panel_dir/<map>.json.
class SidecarPanelSource : public IPanelSource {
 public:
  std::vector<PanelDesc> EnumeratePanels(const std::string& mapName) override;
};
