#pragma once

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "PanelSource.hpp"

// Holds the chamber's portalable wall panels and their marks. Static panels
// are filled once per chamber from an IPanelSource; dynamic (brush-entity)
// panels are replaced every frame with their current pose. Both writes happen
// on the main thread; gRPC threads read under the mutex.
class SurfaceMarkTable {
 public:
  void RebuildFromSource(IPanelSource& source, const std::string& mapName);
  void SetDynamic(std::vector<PanelDesc> dyn);
  bool GetPanelFromMark(int mark, PanelDesc* out);
  std::vector<PanelDesc> Panels();
  void Clear();

 private:
  std::mutex mutex;
  std::vector<PanelDesc> panels;
  std::vector<PanelDesc> dynPanels;
  std::unordered_map<int, size_t> byMark;
};

extern SurfaceMarkTable surfaceMarkTable;
