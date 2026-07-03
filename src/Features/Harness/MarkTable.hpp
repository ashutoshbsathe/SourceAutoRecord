#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

// Assigns a stable integer "mark" to each puzzle entity. Once seen, an entity
// keeps its mark for the rest of the episode even as others spawn/despawn, so
// dynamic chambers (e.g. cube droppers) never renumber existing marks mid-run.
// New entities are appended in deterministic order (index, rounded origin as
// tiebreak), so the same chamber yields the same assignment across runs.
// A respawned entity (new serial) whose classname + targetname matches a
// no-longer-live mark INHERITS that mark: a dropper respawns its cube under
// the same name, so the "same" cube keeps one mark instead of minting a new
// one per fizzle. Droppers spawn the replacement a tick or two BEFORE
// removing the fizzled cube, so a newcomer whose identity-mark is still held
// by a live predecessor stays unmarked for a short grace instead of minting
// a fresh mark; genuine same-name coexistence times out to a fresh mark.
// A mark still never moves off a living entity.
class MarkTable {
 public:
  // Recompute marks from the live server entity list. Walks the engine entity
  // list, so it must run on the MAIN thread; gRPC threads read instead via the
  // mutex-locked GetMark / GetEntityFromMark.
  void RebuildFromWorld();

  // Forward lookup: entity (slot index + serial) -> mark, or 0 if unmarked.
  int GetMark(int entityIndex, uint16_t serial);

  // Reverse lookup: mark -> {entity index, serial}, or {-1, 0} if absent.
  std::pair<int, uint16_t> GetEntityFromMark(int mark);

  void Clear();

 private:
  // key = index << 16 | serial. `assigned` holds every key's mark for the whole
  // episode; forward/reverse mirror only the live entities and are rebuilt each
  // call. All guarded by `mutex`.
  std::mutex mutex;
  std::unordered_map<uint32_t, int> assigned;  // key -> mark (persistent)
  std::unordered_map<uint32_t, int> forward;   // live key -> mark
  std::unordered_map<int, uint32_t> reverse;   // live mark -> key
  // "classname:targetname" -> the first mark handed to that identity, for
  // respawn inheritance. Only consulted when that mark has no live owner.
  std::unordered_map<std::string, int> nameMark;
  // key -> rebuilds spent waiting for the identity-mark to vacate.
  std::unordered_map<uint32_t, int> deferred;
  // The initial cohort never defers (same-name statics get fresh marks at
  // once); only entities appearing after the first rebuild can be respawns.
  bool primed = false;
  int nextMark = 1;  // next mark to hand out
};

extern MarkTable markTable;
