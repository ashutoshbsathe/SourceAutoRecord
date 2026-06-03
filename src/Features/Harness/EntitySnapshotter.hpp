#pragma once
#include <memory>
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

enum class ReadMode : uint8_t {
  DirectOffset,    // memcpy(dst, (char*)se + srcOffset, size)
  AbsOrigin,       // memcpy(dst, &se->abs_origin(), 12)
  AbsAngles,       // memcpy(dst, &se->abs_angles(), 12)
  AbsVelocity,     // memcpy(dst, &se->abs_velocity(), 12)
  CollisionSolid,  // *dst = se->collision().GetSolid()
  CollisionMins,   // memcpy(dst, &se->collision().OBBMins(), 12)
  CollisionMaxs,   // memcpy(dst, &se->collision().OBBMaxs(), 12)
};

struct FieldSlot {
  uint16_t fieldId;
  uint16_t dstOffset;  // byte offset within EntitySlot::fieldBuf
  uint16_t srcOffset;  // byte offset within entity memory (DirectOffset only)
  uint8_t size;
  ReadMode mode;
};

struct ClassLayout {
  uint16_t classId;
  uint16_t fieldBufSize;  // total bytes for all fields
  std::vector<FieldSlot> fields;
};

struct EntitySlot {
  bool alive = false;
  uint16_t serial = 0;
  uint16_t classId = 0;
  uint32_t changeVersion = 0;

  std::unique_ptr<uint8_t[]> fieldBuf;
  uint16_t fieldBufSize = 0;

  std::string className;
  std::string targetName;
};

class EntitySnapshotter {
 public:
  EntitySnapshotter();
  ~EntitySnapshotter() = default;

  void DiscoverSchema();
  void Update();

  // Zero-copy state retrieval
  const EntitySlot& GetSlot(int index) const { return slots[index]; }
  const ClassLayout& GetClassLayout(uint16_t classId) const {
    return classLayouts[classId];
  }

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
  void InitSlot(int index, CEntInfo* info, void* entity);

  mutable std::mutex mutex;
  int currentTick = -1;

  std::vector<EntitySlot> slots;
  std::vector<ClassLayout> classLayouts;

  // Schema definitions
  std::vector<HdemClassDef> classes;
  std::vector<HdemFieldDef> allFields;
  std::unordered_map<std::string, uint16_t> classNameToId;
  std::unordered_map<std::string, uint16_t> fieldNameToId;
  bool schemaDiscovered = false;

  // Member scratch buffer for state comparison to avoid stack allocations
  std::vector<uint8_t> scratchBuf;
};
