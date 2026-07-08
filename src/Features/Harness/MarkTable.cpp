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
#include "Modules/Console.hpp"
#include "Modules/Server.hpp"
#include "Offsets.hpp"
#include "PuzzleAnnotate.hpp"
#include "Utils/SDK/EntityEdict.hpp"
#include "Utils/SDK/Handle.hpp"
#include "Variable.hpp"

Variable sar_harness_mark_debug(
    "sar_harness_mark_debug", "0", 0, 1,
    "Log mark assignments, dropper suppression/release, and the inputs "
    "driving them.\n");

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
  // Stuck-insurance only -- FireUser1 is the release signal. A newborn falls
  // ~150u INSIDE the housing (the template cube's origin sits high above the
  // seat), so the threshold must be far past that; six voxels is decisively
  // outside any dropper.
  constexpr long kSuppressBackstopSq = 768 * 768;
  // A fresh same-name cube at a still-live mark's BIRTH origin is a dropper
  // respawn (stock droppers ping FireUser4 to tag it; custom-Hammer ones
  // don't). This is the "reappeared where the prior one was born" radius --
  // tight enough that a genuine second cube placed elsewhere is excluded.
  // Tunable.
  constexpr long kRespawnBirthSq = 96 * 96;

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
        if (drop && sar_harness_mark_debug.GetBool())
          console->Print(
              "[markdbg] backstop released [%d] (moved %ldu)\n",
              (int)(it->first >> 16),
              std::lround(std::sqrt((double)(dx * dx + dy * dy + dz * dz))));
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
    bool inherited = false;
    if (!c->cname.empty()) {
      auto nm = nameMark.find(c->cname);
      if (nm != nameMark.end()) {
        if (!liveMarks.count(nm->second)) {
          mark = nm->second;
          inherited = true;
        } else {
          // The name's mark is still LIVE (coexistence). A same-name cube back
          // at that mark's birth origin is a dropper respawn -- suppress it so
          // no phantom mark churns; the 768u backstop above frees it if it is
          // ever used (moves off the spawn). Otherwise defer to the inherit
          // grace (a genuine second cube placed elsewhere).
          auto nb = nameBirth.find(c->cname);
          if (nb != nameBirth.end()) {
            long dx = c->rx - std::lround(nb->second.x);
            long dy = c->ry - std::lround(nb->second.y);
            long dz = c->rz - std::lround(nb->second.z);
            if (dx * dx + dy * dy + dz * dz < kRespawnBirthSq) {
              suppressed[c->key] =
                  Vector{(float)c->rx, (float)c->ry, (float)c->rz};
              if (sar_harness_mark_debug.GetBool())
                console->Print("[markdbg] respawn-suppressed [%d] %s @ birth\n",
                               c->index, c->cname.c_str());
              continue;
            }
          }
          if (primed && deferred[c->key]++ < kInheritGrace) {
            if (deferred[c->key] == 1 && sar_harness_mark_debug.GetBool())
              console->Print("[markdbg] deferring [%d] %s (mark %d busy)\n",
                             c->index, c->cname.c_str(), nm->second);
            continue;  // predecessor still live; stay unmarked and retry
          }
        }
      }
    }
    if (!mark) mark = nextMark++;
    assigned[c->key] = mark;
    liveMarks.insert(mark);
    deferred.erase(c->key);
    if (!c->cname.empty()) {
      nameMark.emplace(c->cname, mark);
      nameBirth.emplace(c->cname,
                        Vector{(float)c->rx, (float)c->ry, (float)c->rz});
    }
    if (primed && sar_harness_mark_debug.GetBool())
      console->Print("[markdbg] mark %d -> [%d] %s (%s)\n", mark, c->index,
                     c->cname.empty() ? "<unnamed>" : c->cname.c_str(),
                     inherited ? "inherited" : "fresh");
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

  // The dropper template pings its newborn with FireUser4. The ping goes to
  // a name wildcard, so every same-name cube in the chamber receives it --
  // only a never-marked entity can be the newborn (spawn and ping happen in
  // one event-queue pass; no rebuild can interleave), veterans are left
  // alone.
  if (!strcasecmp(inputName, "FireUser4")) {
    if (!IsHarnessMarkedClass(className)) return;
    const CBaseHandle& h = ((IHandleEntity*)ent)->GetRefEHandle();
    uint32_t key = (static_cast<uint32_t>(h.GetEntryIndex()) << 16) |
                   static_cast<uint16_t>(h.GetSerialNumber());
    Vector origin = SE(ent)->abs_origin();
    std::lock_guard<std::mutex> lock(mutex);
    bool veteran = assigned.count(key) > 0;
    if (!veteran) suppressed[key] = origin;
    if (sar_harness_mark_debug.GetBool())
      console->Print("[markdbg] FireUser4 -> [%d] %s \"%s\": %s\n",
                     h.GetEntryIndex(), className, server->GetEntityName(ent),
                     veteran ? "veteran, kept mark" : "suppressed");
    return;
  }

  // The dropper's release chain pings the cube family with FireUser1 (an
  // already-out cube uses the same ping to dissolve itself); for a
  // suppressed entity it means the dropper is opening for it. Only
  // suppressed keys react, so veterans keep their engine-side semantics.
  if (!strcasecmp(inputName, "FireUser1")) {
    if (!IsHarnessMarkedClass(className)) return;
    const CBaseHandle& h = ((IHandleEntity*)ent)->GetRefEHandle();
    uint32_t key = (static_cast<uint32_t>(h.GetEntryIndex()) << 16) |
                   static_cast<uint16_t>(h.GetSerialNumber());
    std::lock_guard<std::mutex> lock(mutex);
    bool released = suppressed.erase(key) > 0;
    if (sar_harness_mark_debug.GetBool())
      console->Print("[markdbg] FireUser1 -> [%d] \"%s\": %s\n",
                     h.GetEntryIndex(), server->GetEntityName(ent),
                     released ? "released" : "not suppressed, no-op");
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
  nameBirth.clear();
  deferred.clear();
  suppressed.clear();
  primed = false;
  nextMark = 1;
}

// Fresh marks on each map load so numbering restarts per chamber.
ON_EVENT(SESSION_START) { markTable.Clear(); }
