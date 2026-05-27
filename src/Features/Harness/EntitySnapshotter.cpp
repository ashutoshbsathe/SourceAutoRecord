#include "EntitySnapshotter.hpp"

#include <cstring>

#include "Entity.hpp"
#include "Features/EntityList.hpp"
#include "Modules/Engine.hpp"
#include "Modules/Server.hpp"
#include "Offsets.hpp"
#include "harness.pb.h"

template <typename T>
static inline std::vector<uint8_t> ValueToBytes(const T& val) {
  std::vector<uint8_t> buf(sizeof(T));
  std::memcpy(buf.data(), &val, sizeof(T));
  return buf;
}

uint16_t EntitySnapshotter::GetOrAddClass(const std::string& className) {
  auto it = classNameToId.find(className);
  if (it != classNameToId.end()) return it->second;
  uint16_t id = static_cast<uint16_t>(classes.size());
  classNameToId[className] = id;
  classes.push_back({id, className, {}});
  return id;
}

uint16_t EntitySnapshotter::GetOrAddField(const std::string& fieldName,
                                          HdemFieldType type) {
  auto it = fieldNameToId.find(fieldName);
  if (it != fieldNameToId.end()) return it->second;
  uint16_t id = static_cast<uint16_t>(allFields.size());
  fieldNameToId[fieldName] = id;
  allFields.push_back({id, fieldName, type});
  return id;
}

void EntitySnapshotter::DiscoverSchema() {
  if (schemaDiscovered) return;

  classes.clear();
  allFields.clear();
  classNameToId.clear();
  fieldNameToId.clear();

  // Pre-initialize well-known fields at their corresponding enum indices in
  // allFields
  struct WellKnownField {
    HdemWellKnownField id;
    std::string name;
    HdemFieldType type;
  };

  static const WellKnownField wellKnownFields[] = {
      {HDEM_FIELD_ORIGIN, "m_vecAbsOrigin", HDEM_VEC3},
      {HDEM_FIELD_ANGLES, "m_angAbsRotation", HDEM_VEC3},
      {HDEM_FIELD_VELOCITY, "m_vecAbsVelocity", HDEM_VEC3},
      {HDEM_FIELD_ACTIVATED, "m_bActivated", HDEM_BOOL},
      {HDEM_FIELD_ISPORTAL2, "m_bIsPortal2", HDEM_BOOL},
      {HDEM_FIELD_LINKEDPORTAL, "m_hLinkedPortal", HDEM_HANDLE},
      {HDEM_FIELD_HEALTH, "m_iHealth", HDEM_INT32},
      {HDEM_FIELD_FLAGS, "m_fFlags", HDEM_INT32},
      {HDEM_FIELD_OWNERENTITY, "m_hOwnerEntity", HDEM_HANDLE},
      {HDEM_FIELD_LOCKED, "m_bLocked", HDEM_BOOL},
      {HDEM_FIELD_TOGGLESTATE, "m_toggle_state", HDEM_INT32},
      {HDEM_FIELD_CLASSNAME, "m_iClassname", HDEM_STRING},
      {HDEM_FIELD_NAME, "m_iName", HDEM_STRING},
      {HDEM_FIELD_SOLIDTYPE, "m_nSolidType", HDEM_BYTE},
      {HDEM_FIELD_MINS, "m_vecMins", HDEM_VEC3},
      {HDEM_FIELD_MAXS, "m_vecMaxs", HDEM_VEC3},
  };

  allFields.resize(sizeof(wellKnownFields) / sizeof(wellKnownFields[0]));
  for (const auto& f : wellKnownFields) {
    allFields[f.id] = {static_cast<uint16_t>(f.id), f.name, f.type};
    fieldNameToId[f.name] = static_cast<uint16_t>(f.id);
  }

  RegisterClassSchema("prop_portal");
  RegisterClassSchema("player");
  RegisterClassSchema("portal_player");
  RegisterClassSchema("prop_physics");
  RegisterClassSchema("prop_dynamic");
  RegisterClassSchema("trigger_portal_cleanser");
  RegisterClassSchema("prop_weighted_cube");
  RegisterClassSchema("prop_button");
  RegisterClassSchema("func_weight_button");
  RegisterClassSchema("prop_testchamber_door");

  if (!server || !entityList) return;

  for (int i = 0; i < Offsets::NUM_ENT_ENTRIES; ++i) {
    auto info = entityList->GetEntityInfoByIndex(i);
    if (!info || !info->m_pEntity) continue;

    auto ent = info->m_pEntity;
    const char* className = server->GetEntityClassName(ent);
    if (!className) continue;

    RegisterClassSchema(className);
  }

  schemaDiscovered = true;
}

void EntitySnapshotter::RegisterClassSchema(const std::string& className) {
  uint16_t cid = GetOrAddClass(className);
  auto& cls = classes[cid];
  if (cls.fields.empty()) {
    cls.fields.push_back(allFields[HDEM_FIELD_ORIGIN]);
    cls.fields.push_back(allFields[HDEM_FIELD_ANGLES]);
    cls.fields.push_back(allFields[HDEM_FIELD_VELOCITY]);
    cls.fields.push_back(allFields[HDEM_FIELD_HEALTH]);
    cls.fields.push_back(allFields[HDEM_FIELD_FLAGS]);
    cls.fields.push_back(allFields[HDEM_FIELD_OWNERENTITY]);
    cls.fields.push_back(allFields[HDEM_FIELD_SOLIDTYPE]);
    cls.fields.push_back(allFields[HDEM_FIELD_MINS]);
    cls.fields.push_back(allFields[HDEM_FIELD_MAXS]);

    if (className == "prop_portal") {
      cls.fields.push_back(allFields[HDEM_FIELD_ACTIVATED]);
      cls.fields.push_back(allFields[HDEM_FIELD_ISPORTAL2]);
      cls.fields.push_back(allFields[HDEM_FIELD_LINKEDPORTAL]);
    } else if (className == "prop_button") {
      cls.fields.push_back(allFields[HDEM_FIELD_LOCKED]);
    } else if (className == "func_weight_button") {
      cls.fields.push_back(allFields[HDEM_FIELD_TOGGLESTATE]);
    } else if (className == "prop_testchamber_door") {
      cls.fields.push_back(allFields[HDEM_FIELD_TOGGLESTATE]);
      cls.fields.push_back(allFields[HDEM_FIELD_LOCKED]);
    }
  }
}

const HdemFieldDef* EntitySnapshotter::GetFieldDef(uint16_t fieldId) const {
  if (fieldId < allFields.size()) {
    return &allFields[fieldId];
  }
  return nullptr;
}

void EntitySnapshotter::Update() {
  if (!server || !entityList) return;

  std::lock_guard<std::mutex> lock(mutex);
  currentTick = server->gpGlobals->tickcount;
  activeEntities.clear();

  for (int i = 0; i < Offsets::NUM_ENT_ENTRIES; ++i) {
    auto info = entityList->GetEntityInfoByIndex(i);
    if (!info || !info->m_pEntity) continue;

    auto ent = info->m_pEntity;
    const char* classNameCStr = server->GetEntityClassName(ent);
    if (!classNameCStr) continue;

    auto itClass = classNameToId.find(classNameCStr);
    if (itClass == classNameToId.end()) {
      continue;
    }
    uint16_t classId = itClass->second;
    const auto& cls = classes[classId];

    TrackedEntity tracked;
    tracked.entityIndex = i;
    tracked.serialNumber = static_cast<uint16_t>(info->m_SerialNumber);
    tracked.classId = classId;
    tracked.className = classNameCStr;
    const char* nameCStr = server->GetEntityName(ent);
    tracked.targetName = nameCStr ? nameCStr : "";

    ServerEnt* se = SE(ent);
    tracked.position = se->abs_origin();
    tracked.angles = se->abs_angles();
    tracked.velocity = se->abs_velocity();

    // Populate all field values as raw bytes
    for (const auto& field : cls.fields) {
      if (field.fieldId == HDEM_FIELD_ORIGIN) {
        tracked.fieldValues[field.fieldId] = ValueToBytes(tracked.position);
      } else if (field.fieldId == HDEM_FIELD_ANGLES) {
        tracked.fieldValues[field.fieldId] = ValueToBytes(tracked.angles);
      } else if (field.fieldId == HDEM_FIELD_VELOCITY) {
        tracked.fieldValues[field.fieldId] = ValueToBytes(tracked.velocity);
      } else if (field.fieldId == HDEM_FIELD_SOLIDTYPE) {
        uint8_t solid = SOLID_NONE;
        try {
          ICollideable* coll = &se->collision();
          if (coll) solid = static_cast<uint8_t>(coll->GetSolid());
        } catch (...) {
        }
        tracked.fieldValues[field.fieldId] = ValueToBytes(solid);
      } else if (field.fieldId == HDEM_FIELD_MINS) {
        Vector mins(0, 0, 0);
        try {
          ICollideable* coll = &se->collision();
          if (coll) mins = coll->OBBMins();
        } catch (...) {
        }
        tracked.fieldValues[field.fieldId] = ValueToBytes(mins);
      } else if (field.fieldId == HDEM_FIELD_MAXS) {
        Vector maxs(0, 0, 0);
        try {
          ICollideable* coll = &se->collision();
          if (coll) maxs = coll->OBBMaxs();
        } catch (...) {
        }
        tracked.fieldValues[field.fieldId] = ValueToBytes(maxs);
      } else {
        try {
          auto val = EntField::getServerOffset(ent, field.name.c_str());
          if (val.first != 0 && val.second != EntField::Type::NONE) {
            size_t size = HdemFieldSize(field.type);
            if (field.fieldId == HDEM_FIELD_TOGGLESTATE &&
                val.second == EntField::Type::CHAR) {
              int intVal = *(char*)((uintptr_t)se + val.first);
              tracked.fieldValues[field.fieldId] = ValueToBytes(intVal);
            } else {
              std::vector<uint8_t> buf(size);
              std::memcpy(buf.data(), (void*)((uintptr_t)se + val.first), size);
              tracked.fieldValues[field.fieldId] = buf;
            }
          } else {
            tracked.fieldValues[field.fieldId] =
                std::vector<uint8_t>(HdemFieldSize(field.type), 0);
          }
        } catch (...) {
          tracked.fieldValues[field.fieldId] =
              std::vector<uint8_t>(HdemFieldSize(field.type), 0);
        }
      }
    }

    activeEntities[i] = tracked;
  }
}

void EntitySnapshotter::GetSnapshot(std::vector<TrackedEntity>& outEntities,
                                    int& outTick) {
  std::lock_guard<std::mutex> lock(mutex);
  outTick = currentTick;
  outEntities.clear();
  outEntities.reserve(activeEntities.size());
  for (const auto& pair : activeEntities) {
    outEntities.push_back(pair.second);
  }
}
