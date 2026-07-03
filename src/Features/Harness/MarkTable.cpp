#include "MarkTable.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <unordered_set>
#include <vector>

#include "Entity.hpp"
#include "Event.hpp"
#include "Features/EntityList.hpp"
#include "Modules/Server.hpp"
#include "Offsets.hpp"
#include "PuzzleAnnotate.hpp"
#include "Utils/SDK/EntityEdict.hpp"

MarkTable markTable;

// Respawn-stable identity: classname + targetname, with a template name-fixup
// suffix ("&0001") stripped. Empty for unnamed entities.
static std::string CanonicalName(const char* className, const char* name) {
  if (!name || !*name) return {};
  std::string s(className);
  s += ':';
  s += name;
  size_t amp = s.rfind('&');
  if (amp != std::string::npos && amp + 1 < s.size()) {
    bool digits = true;
    for (size_t i = amp + 1; i < s.size(); ++i)
      digits = digits && std::isdigit(static_cast<unsigned char>(s[i]));
    if (digits) s.resize(amp);
  }
  return s;
}

void MarkTable::RebuildFromWorld() {
  if (!server || !entityList) return;

  // Gather the currently marked entities. The walk is unlocked; only the
  // assign + map swap below is guarded.
  struct Cand {
    uint32_t key;  // index << 16 | serial
    int index;
    long rx, ry, rz;
    std::string cname;
  };
  std::vector<Cand> cands;
  for (int i = 0; i < Offsets::NUM_ENT_ENTRIES; ++i) {
    auto info = entityList->GetEntityInfoByIndex(i);
    if (!info || !info->m_pEntity) continue;
    const char* className = server->GetEntityClassName(info->m_pEntity);
    if (!IsHarnessMarkedEntity(info->m_pEntity, className)) continue;
    Vector o = SE(info->m_pEntity)->abs_origin();
    uint32_t key = (static_cast<uint32_t>(i) << 16) |
                   static_cast<uint16_t>(info->m_SerialNumber);
    cands.push_back(
        {key, i, std::lround(o.x), std::lround(o.y), std::lround(o.z),
         CanonicalName(className, server->GetEntityName(info->m_pEntity))});
  }

  std::lock_guard<std::mutex> lock(mutex);

  // Assign marks to unseen entities in deterministic order (index, rounded
  // origin), appended after existing ones so a mark never moves on
  // spawn/despawn. An entity whose canonical name held a mark with no living
  // owner inherits it (a respawned dropper cube keeps its number); otherwise
  // a fresh mark is appended.
  std::unordered_set<int> liveMarks;
  for (const auto& c : cands) {
    auto it = assigned.find(c.key);
    if (it != assigned.end()) liveMarks.insert(it->second);
  }
  std::vector<const Cand*> fresh;
  for (const auto& c : cands)
    if (!assigned.count(c.key)) fresh.push_back(&c);
  std::sort(fresh.begin(), fresh.end(), [](const Cand* a, const Cand* b) {
    if (a->index != b->index) return a->index < b->index;
    if (a->rx != b->rx) return a->rx < b->rx;
    if (a->ry != b->ry) return a->ry < b->ry;
    return a->rz < b->rz;
  });
  for (const Cand* c : fresh) {
    int mark = 0;
    if (!c->cname.empty()) {
      auto nm = nameMark.find(c->cname);
      if (nm != nameMark.end() && !liveMarks.count(nm->second))
        mark = nm->second;
    }
    if (!mark) mark = nextMark++;
    assigned[c->key] = mark;
    liveMarks.insert(mark);
    if (!c->cname.empty()) nameMark.emplace(c->cname, mark);
  }

  // Mirror only live entities; a despawned mark drops out so its reverse
  // lookup correctly misses.
  forward.clear();
  reverse.clear();
  for (const auto& c : cands) {
    int mark = assigned.at(c.key);  // every live key was just assigned above
    forward[c.key] = mark;
    reverse[mark] = c.key;
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
  assigned.clear();
  forward.clear();
  reverse.clear();
  nameMark.clear();
  nextMark = 1;
}

// Fresh marks on each map load so numbering restarts per chamber.
ON_EVENT(SESSION_START) { markTable.Clear(); }
