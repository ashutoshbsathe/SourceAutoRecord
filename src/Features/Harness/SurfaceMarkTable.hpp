#pragma once

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "PanelSource.hpp"

// Holds the chamber's portalable wall panels and their marks. Filled once per
// chamber from an IPanelSource on the main thread; gRPC threads read under the
// mutex.
class SurfaceMarkTable {
 public:
  void RebuildFromSource(IPanelSource& source, const std::string& mapName);
  bool GetPanelFromMark(int mark, PanelDesc* out);
  std::vector<PanelDesc> Panels();
  void Clear();

 private:
  std::mutex mutex;
  std::vector<PanelDesc> panels;
  std::unordered_map<int, size_t> byMark;
};

extern SurfaceMarkTable surfaceMarkTable;
