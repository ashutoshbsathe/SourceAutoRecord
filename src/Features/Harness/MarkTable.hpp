#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

#include "Utils/SDK/Math.hpp"

// Assigns a stable integer "mark" to each puzzle entity. Once seen, an entity
// keeps its mark for the rest of the episode even as others spawn/despawn, so
// dynamic chambers (e.g. cube droppers) never renumber existing marks mid-run.
// New entities are appended in deterministic order (index, rounded origin as
// tiebreak), so the same chamber yields the same assignment across runs.
//
// Dropper handling: a cube waiting inside a dropper tube is not an affordance
// (it can't be reached), so it carries NO mark. The dropper template pings
// its newborn with FireUser4 -- that input suppresses the entity -- and the
// dropper releases by Disable-ing the clip brush the cube rests on, which
// unsuppresses it. Once out, an entity whose classname + targetname matches
// a no-longer-live mark INHERITS that mark, so the dropper's "same" cube
// keeps one number across respawns; while the predecessor still lives (e.g.
// its corpse is mid-dissolve), the newcomer waits a short grace unmarked
// rather than minting a fresh mark. Genuine same-name coexistence times out
// to a fresh mark. A mark never moves off a living entity.
class MarkTable {
 public:
  // Recompute marks from the live server entity list. Walks the engine entity
  // list, so it must run on the MAIN thread; gRPC threads read instead via the
  // mutex-locked GetMark / GetEntityFromMark.
  void RebuildFromWorld();

  // Fed every entity input by the AcceptInput hook (game thread). FireUser4
  // on a markable newborn tags it dropper-held; Disable on any entity
  // releases suppressed entities resting inside that entity's box.
  void OnEntityInput(void* ent, const char* className, const char* inputName);

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
  // "classname:targetname" -> the origin its first cube was born at. A fresh
  // same-name cube reappearing here while the mark is still live is a dropper
  // respawn (suppress it) rather than a genuine second cube (mark it).
  std::unordered_map<std::string, Vector> nameBirth;
  // key -> rebuilds spent waiting for the identity-mark to vacate.
  std::unordered_map<uint32_t, int> deferred;
  // Dropper-held entities: key -> origin at tag time. Unmarked while here;
  // released by the clip Disable, or by the moved-a-voxel backstop.
  std::unordered_map<uint32_t, Vector> suppressed;
  // The initial cohort never defers (same-name statics get fresh marks at
  // once); only entities appearing after the first rebuild can be respawns.
  bool primed = false;
  int nextMark = 1;  // next mark to hand out
};

extern MarkTable markTable;
