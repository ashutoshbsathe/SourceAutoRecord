#pragma once

#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <utility>

// Assigns a stable integer "mark" to each puzzle entity. Once seen, an entity
// keeps its mark for the rest of the episode even as others spawn/despawn, so
// dynamic chambers (e.g. cube droppers) never renumber existing marks mid-run.
// New entities are appended in deterministic order (index, rounded origin as
// tiebreak), so the same chamber yields the same assignment across runs.
// A respawned entity (new serial) counts as new and gets a fresh mark, so marks
// only grow over an episode and never shift under an entity.
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
  int nextMark = 1;                            // next mark to hand out
};

extern MarkTable markTable;
