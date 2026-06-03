#include "MarkTable.hpp"

#include "Event.hpp"

MarkTable markTable;

int MarkTable::GetMark(int entityIndex, uint16_t serial) {
  uint32_t key = (static_cast<uint32_t>(entityIndex) << 16) | serial;
  auto it = marks.find(key);
  if (it != marks.end()) return it->second;
  int mark = nextMark++;
  marks[key] = mark;
  return mark;
}

void MarkTable::Clear() {
  marks.clear();
  nextMark = 1;
}

// Fresh marks on each map load so numbering restarts per chamber.
ON_EVENT(SESSION_START) { markTable.Clear(); }
