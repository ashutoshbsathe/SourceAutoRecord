#include "MarkTable.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "Entity.hpp"
#include "Event.hpp"
#include "Features/EntityList.hpp"
#include "HarnessAnnotate.hpp"
#include "Modules/Server.hpp"
#include "Offsets.hpp"
#include "Utils/SDK/EntityEdict.hpp"

MarkTable markTable;

void MarkTable::RebuildFromWorld() {
  if (!server || !entityList) return;

  // Gather marked entities and number them deterministically: by index, with
  // rounded origin as a tiebreak. The walk is unlocked; only the swap is
  // guarded.
  struct Cand {
    int index;
    uint16_t serial;
    long rx, ry, rz;
  };
  std::vector<Cand> cands;
  for (int i = 0; i < Offsets::NUM_ENT_ENTRIES; ++i) {
    auto info = entityList->GetEntityInfoByIndex(i);
    if (!info || !info->m_pEntity) continue;
    const char* className = server->GetEntityClassName(info->m_pEntity);
    if (!IsHarnessMarkedClass(className)) continue;
    Vector o = SE(info->m_pEntity)->abs_origin();
    cands.push_back({i, static_cast<uint16_t>(info->m_SerialNumber),
                     std::lround(o.x), std::lround(o.y), std::lround(o.z)});
  }

  std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) {
    if (a.index != b.index) return a.index < b.index;
    if (a.rx != b.rx) return a.rx < b.rx;
    if (a.ry != b.ry) return a.ry < b.ry;
    return a.rz < b.rz;
  });

  std::lock_guard<std::mutex> lock(mutex);
  forward.clear();
  reverse.clear();
  int mark = 1;
  for (const auto& c : cands) {
    uint32_t key = (static_cast<uint32_t>(c.index) << 16) | c.serial;
    forward[key] = mark;
    reverse[mark] = key;
    ++mark;
  }
}

int MarkTable::GetMark(int entityIndex, uint16_t serial) {
  uint32_t key = (static_cast<uint32_t>(entityIndex) << 16) | serial;
  std::lock_guard<std::mutex> lock(mutex);
  auto it = forward.find(key);
  return it == forward.end() ? 0 : it->second;
}

std::pair<int, uint16_t> MarkTable::GetEntityFromMark(int mark) {
  std::lock_guard<std::mutex> lock(mutex);
  auto it = reverse.find(mark);
  if (it == reverse.end()) return {-1, 0};
  uint32_t key = it->second;
  return {static_cast<int>(key >> 16), static_cast<uint16_t>(key & 0xFFFF)};
}

void MarkTable::Clear() {
  std::lock_guard<std::mutex> lock(mutex);
  forward.clear();
  reverse.clear();
}

// Fresh marks on each map load so numbering restarts per chamber.
ON_EVENT(SESSION_START) { markTable.Clear(); }
