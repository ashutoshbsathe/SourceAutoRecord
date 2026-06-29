#include "SurfaceMarkTable.hpp"

#include <utility>

SurfaceMarkTable surfaceMarkTable;

void SurfaceMarkTable::RebuildFromSource(IPanelSource& source,
                                         const std::string& mapName) {
  auto fresh = source.EnumeratePanels(mapName);
  std::lock_guard<std::mutex> lock(mutex);
  panels = std::move(fresh);
  byMark.clear();
  for (size_t i = 0; i < panels.size(); ++i) byMark[panels[i].mark] = i;
}

bool SurfaceMarkTable::GetPanelFromMark(int mark, PanelDesc* out) {
  std::lock_guard<std::mutex> lock(mutex);
  auto it = byMark.find(mark);
  if (it == byMark.end()) return false;
  *out = panels[it->second];
  return true;
}

std::vector<PanelDesc> SurfaceMarkTable::Panels() {
  std::lock_guard<std::mutex> lock(mutex);
  return panels;
}

void SurfaceMarkTable::Clear() {
  std::lock_guard<std::mutex> lock(mutex);
  panels.clear();
  byMark.clear();
}
