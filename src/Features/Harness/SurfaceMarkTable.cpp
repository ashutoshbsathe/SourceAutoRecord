#include "SurfaceMarkTable.hpp"

#include <utility>

SurfaceMarkTable surfaceMarkTable;

void SurfaceMarkTable::RebuildFromSource(IPanelSource& source,
                                         const std::string& mapName) {
  auto fresh = source.EnumeratePanels(mapName);
  std::lock_guard<std::mutex> lock(mutex);
  panels = std::move(fresh);
  dynPanels.clear();
  byMark.clear();
  for (size_t i = 0; i < panels.size(); ++i) byMark[panels[i].mark] = i;
}

void SurfaceMarkTable::SetDynamic(std::vector<PanelDesc> dyn) {
  std::lock_guard<std::mutex> lock(mutex);
  dynPanels = std::move(dyn);
}

bool SurfaceMarkTable::GetPanelFromMark(int mark, PanelDesc* out) {
  std::lock_guard<std::mutex> lock(mutex);
  auto it = byMark.find(mark);
  if (it != byMark.end()) {
    *out = panels[it->second];
    return true;
  }
  for (const auto& p : dynPanels)
    if (p.mark == mark) {
      *out = p;
      return true;
    }
  return false;
}

std::vector<PanelDesc> SurfaceMarkTable::Panels() {
  std::lock_guard<std::mutex> lock(mutex);
  std::vector<PanelDesc> all = panels;
  all.insert(all.end(), dynPanels.begin(), dynPanels.end());
  return all;
}

void SurfaceMarkTable::Clear() {
  std::lock_guard<std::mutex> lock(mutex);
  panels.clear();
  dynPanels.clear();
  byMark.clear();
}
