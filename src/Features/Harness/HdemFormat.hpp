#pragma once
#include <cstddef>
#include <cstdint>

// Magic: "HDEM", version, flags
constexpr uint32_t HDEM_MAGIC = 0x4D454448;  // "HDEM" in Little-Endian
constexpr uint16_t HDEM_VERSION = 1;

enum HdemFieldType : uint8_t {
  HDEM_FLOAT = 0,
  HDEM_INT32 = 1,
  HDEM_VEC3 = 2,
  HDEM_BOOL = 3,
  HDEM_STRING = 4,
  HDEM_HANDLE = 5,
  HDEM_BYTE = 6,
  HDEM_SHORT = 7,
  HDEM_COLOR = 8,
};

enum HdemEntityFlags : uint8_t {
  HDEM_ENT_ALIVE = 0x01,
  HDEM_ENT_DORMANT = 0x02,
  HDEM_ENT_DELETED = 0x04,
  HDEM_ENT_FULL_SNAPSHOT = 0x08,
};

// Size lookup by type
inline size_t HdemFieldSize(HdemFieldType t) {
  switch (t) {
    case HDEM_FLOAT:
    case HDEM_INT32:
    case HDEM_HANDLE:
    case HDEM_COLOR:
      return 4;
    case HDEM_VEC3:
      return 12;
    case HDEM_BOOL:
    case HDEM_BYTE:
      return 1;
    case HDEM_SHORT:
      return 2;
    default:
      return 0;  // STRING is variable
  }
}
