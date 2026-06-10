#pragma once

#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <utility>

// Assigns a stable, deterministic integer "mark" to each puzzle entity.
// RebuildFromWorld() recomputes the numbering from world state -- entities
// sorted by index (rounded origin as tiebreak), numbered 1..N -- so the same
// chamber yields the same mark<->entity assignment across runs and save/loads,
// which transcript comparability and replay depend on.
//
// The number drawn on the frame == the mark in the snapshot == the anchor a
// macro verb resolves. Marks are dense 1..N, so they shift if an entity
// spawns/despawns mid-episode; fine for the static hand-authored chambers.
class MarkTable {
 public:
  // Recompute marks from the live server entity list. Idempotent for a frozen
  // world; safe to call from the render (main) and Observe (gRPC) threads.
  void RebuildFromWorld();

  // Forward lookup: entity (slot index + serial) -> mark, or 0 if unmarked.
  int GetMark(int entityIndex, uint16_t serial);

  // Reverse lookup: mark -> {entity index, serial}, or {-1, 0} if absent.
  std::pair<int, uint16_t> GetEntityFromMark(int mark);

  void Clear();

 private:
  // index << 16 | serial  <->  mark, kept in lockstep by RebuildFromWorld.
  std::mutex mutex;
  std::unordered_map<uint32_t, int> forward;  // key -> mark
  std::unordered_map<int, uint32_t> reverse;  // mark -> key
};

extern MarkTable markTable;
