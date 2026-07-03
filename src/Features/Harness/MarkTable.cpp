#include "MarkTable.hpp"

#include <strings.h>  // strcasecmp

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
#include "Utils/SDK/Handle.hpp"

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
  // Covers the identity-mark being briefly held by the predecessor's corpse
  // (a fizzler dissolve runs ~2 s); the wait is invisible -- a just-released
  // cube is simply unmarked while it falls.
  constexpr int kInheritGrace = 150;
  // A dropper tube settles a cube within ~20u; a full voxel of travel means
  // it is out of any dropper, however the release was wired.
  constexpr long kSuppressBackstopSq = 128 * 128;

  std::unordered_set<int> liveMarks;
  std::unordered_set<uint32_t> liveKeys;
  for (const auto& c : cands) {
    liveKeys.insert(c.key);
    auto it = assigned.find(c.key);
    if (it != assigned.end()) liveMarks.insert(it->second);
  }

  for (auto it = suppressed.begin(); it != suppressed.end();) {
    bool drop = !liveKeys.count(it->first);
    if (!drop) {
      for (const auto& c : cands) {
        if (c.key != it->first) continue;
        long dx = c.rx - std::lround(it->second.x);
        long dy = c.ry - std::lround(it->second.y);
        long dz = c.rz - std::lround(it->second.z);
        drop = dx * dx + dy * dy + dz * dz > kSuppressBackstopSq;
        break;
      }
    }
    it = drop ? suppressed.erase(it) : std::next(it);
  }

  std::vector<const Cand*> fresh;
  for (const auto& c : cands)
    if (!assigned.count(c.key) && !suppressed.count(c.key)) fresh.push_back(&c);
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
      if (nm != nameMark.end()) {
        if (!liveMarks.count(nm->second))
          mark = nm->second;
        else if (primed && deferred[c->key]++ < kInheritGrace)
          continue;  // predecessor still live; stay unmarked and retry
      }
    }
    if (!mark) mark = nextMark++;
    assigned[c->key] = mark;
    liveMarks.insert(mark);
    deferred.erase(c->key);
    if (!c->cname.empty()) nameMark.emplace(c->cname, mark);
  }
  primed = true;
  for (auto it = deferred.begin(); it != deferred.end();)
    it = liveKeys.count(it->first) ? std::next(it) : deferred.erase(it);

  // Mirror only live entities; a despawned mark drops out so its reverse
  // lookup correctly misses. A deferred newcomer has no mark yet.
  forward.clear();
  reverse.clear();
  for (const auto& c : cands) {
    auto it = assigned.find(c.key);
    if (it == assigned.end()) continue;
    forward[c.key] = it->second;
    reverse[it->second] = c.key;
  }
}

void MarkTable::OnEntityInput(void* ent, const char* className,
                              const char* inputName) {
  if (!ent || !className || !inputName) return;

  // The dropper template pings its newborn with FireUser4: tag it as held.
  // Retract any mark a same-tick rebuild already handed it -- nothing can
  // have observed a mark younger than one frame.
  if (!strcasecmp(inputName, "FireUser4")) {
    if (!IsHarnessMarkedClass(className)) return;
    const CBaseHandle& h = ((IHandleEntity*)ent)->GetRefEHandle();
    uint32_t key = (static_cast<uint32_t>(h.GetEntryIndex()) << 16) |
                   static_cast<uint16_t>(h.GetSerialNumber());
    Vector origin = SE(ent)->abs_origin();
    std::lock_guard<std::mutex> lock(mutex);
    assigned.erase(key);
    suppressed[key] = origin;
    return;
  }

  // The dropper releases by Disable-ing the clip brush the cube rests on:
  // unsuppress anything held inside that entity's box (the cube sits ~20u
  // above the thin clip, hence the slack).
  if (!strcasecmp(inputName, "Disable")) {
    std::lock_guard<std::mutex> lock(mutex);
    if (suppressed.empty() || !entityList) return;
    auto se = SE(ent);
    Vector o = se->abs_origin();
    Vector mins = o + se->collision().OBBMins() - Vector{48, 48, 48};
    Vector maxs = o + se->collision().OBBMaxs() + Vector{48, 48, 48};
    for (auto it = suppressed.begin(); it != suppressed.end();) {
      int idx = static_cast<int>(it->first >> 16);
      auto info = entityList->GetEntityInfoByIndex(idx);
      bool inside = false;
      if (info && info->m_pEntity &&
          static_cast<uint16_t>(info->m_SerialNumber) ==
              static_cast<uint16_t>(it->first & 0xFFFF)) {
        Vector p = SE(info->m_pEntity)->abs_origin();
        inside = p.x >= mins.x && p.x <= maxs.x && p.y >= mins.y &&
                 p.y <= maxs.y && p.z >= mins.z && p.z <= maxs.z;
      }
      it = inside ? suppressed.erase(it) : std::next(it);
    }
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
  deferred.clear();
  suppressed.clear();
  primed = false;
  nextMark = 1;
}

// Fresh marks on each map load so numbering restarts per chamber.
ON_EVENT(SESSION_START) { markTable.Clear(); }
