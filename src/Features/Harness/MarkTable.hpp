#pragma once

#include <cstdint>
#include <unordered_map>

// Assigns a stable, small integer "mark" to each entity, keyed by its slot
// index + serial number so a reused slot gets a fresh mark. Marks are handed
// out in order of first request and persist for the session (cleared on
// SESSION_START). Shared by the annotation overlay (A3) and, later, entity
// telemetry (C7) so the number drawn on the frame matches the mark in the
// snapshot.
class MarkTable {
 public:
  // Returns the entity's mark, assigning the next free one on first request.
  int GetMark(int entityIndex, uint16_t serial);
  void Clear();

 private:
  std::unordered_map<uint32_t, int> marks;  // key = index << 16 | serial
  int nextMark = 1;
};

extern MarkTable markTable;
