#pragma once
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "Entity.hpp"
#include "HdemFormat.hpp"
#include "Utils/SDK.hpp"

// Forward declare protobuf class to avoid including it in header
namespace portal2_harness {
class EntitySnapshot;
}

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
  HDEM_FIELD_SOLIDTYPE = 13,
  HDEM_FIELD_MINS = 14,
  HDEM_FIELD_MAXS = 15,
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

struct TrackedEntity {
  int entityIndex;
  uint16_t serialNumber;
  uint16_t classId;
  std::string className;
  std::string targetName;
  Vector position;
  QAngle angles;
  Vector velocity;
  // fieldId -> raw bytes of value
  std::unordered_map<uint16_t, std::vector<uint8_t>> fieldValues;
};

class EntitySnapshotter {
 public:
  EntitySnapshotter() = default;
  ~EntitySnapshotter() = default;

  void DiscoverSchema();
  void Update();

  // Thread-safe state retrieval
  void GetSnapshot(std::vector<TrackedEntity>& outEntities, int& outTick);
  void GetSnapshotAndSchema(std::vector<TrackedEntity>& outEntities,
                            int& outTick, std::vector<HdemClassDef>& outClasses,
                            std::vector<HdemFieldDef>& outFields);

  // Schema accessors
  const std::vector<HdemClassDef>& GetClasses() const { return classes; }
  const std::vector<HdemFieldDef>& GetFields() const { return allFields; }
  const HdemFieldDef* GetFieldDef(uint16_t fieldId) const;

 private:
  uint16_t GetOrAddClass(const std::string& className);
  uint16_t GetOrAddField(const std::string& fieldName, HdemFieldType type);
  void RegisterClassSchema(const std::string& className);
  void DiscoverSendTableFields(const std::string& className,
                               struct SendTable* table);

  std::mutex mutex;
  int currentTick = -1;
  std::unordered_map<int, TrackedEntity> activeEntities;

  // Schema definitions
  std::vector<HdemClassDef> classes;
  std::vector<HdemFieldDef> allFields;
  std::unordered_map<std::string, uint16_t> classNameToId;
  std::unordered_map<std::string, uint16_t> fieldNameToId;
  bool schemaDiscovered = false;

  struct ResolvedField {
    uint16_t fieldId;
    size_t offset;
    EntField::Type type;
    size_t size;
  };
  struct ResolvedClass {
    bool resolved = false;
    std::vector<ResolvedField> fields;
  };
  std::vector<ResolvedClass> resolvedClasses;
};
