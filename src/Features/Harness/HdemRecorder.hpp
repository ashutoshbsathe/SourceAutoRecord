#pragma once
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "HdemFormat.hpp"
#include "Utils/SDK.hpp"

enum HdemWellKnownField : uint16_t {
  HDEM_FIELD_ORIGIN = 0,
  HDEM_FIELD_ANGLES = 1,
  HDEM_FIELD_VELOCITY = 2,
  HDEM_FIELD_ACTIVATED = 3,
  HDEM_FIELD_ISPORTAL2 = 4,
  HDEM_FIELD_LINKEDPORTAL = 5,
  HDEM_FIELD_HEALTH = 6,
  HDEM_FIELD_FLAGS = 7,
  HDEM_FIELD_OWNERENTITY = 8,
  HDEM_FIELD_LOCKED = 9,
  HDEM_FIELD_TOGGLESTATE = 10,
  HDEM_FIELD_CLASSNAME = 11,
  HDEM_FIELD_NAME = 12,
};

struct HdemFieldDef {
  uint16_t fieldId;
  std::string name;
  HdemFieldType type;
};

struct HdemClassDef {
  uint16_t classId;
  std::string name;
  std::vector<HdemFieldDef> fields;
};

class HdemRecorder {
 public:
  HdemRecorder() = default;
  ~HdemRecorder();

  bool Start(const std::string& path, const std::string& mapName,
             float tickrate);
  void Stop();
  bool IsActive() const { return isActive; }
  void RecordTick(int tickNumber);
  void DiscoverEntities();  // Scan entity list, build class/field tables

 private:
  uint16_t GetOrAddClass(const std::string& className);
  uint16_t GetOrAddField(const std::string& fieldName, HdemFieldType type);

  void WriteHeader(const std::string& mapName, float tickrate);
  void WriteClassTable();
  void WriteFieldTable();

  std::ofstream file;
  std::streampos schemaOffsetPos;
  bool isActive = false;
  bool headerWritten = false;
  bool schemaDiscovered = false;

  // Schema definitions
  std::vector<HdemClassDef> classes;
  std::vector<HdemFieldDef> allFields;
  std::unordered_map<std::string, uint16_t> classNameToId;
  std::unordered_map<std::string, uint16_t> fieldNameToId;

  // Delta tracking: entityIndex -> last written field values (raw bytes)
  std::unordered_map<int, std::vector<uint8_t>> lastEntityState;
  // Track which entity serials existed last tick
  std::unordered_map<int, int> lastEntitySerial;

  // Reused per-tick buffer to eliminate dynamic allocation overhead
  std::vector<uint8_t> tickBuffer;

  size_t totalBytes = 0;
  size_t totalTicks = 0;
};
