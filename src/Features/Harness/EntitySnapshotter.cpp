#include "EntitySnapshotter.hpp"

#include <cstring>

#include "Entity.hpp"
#include "Features/EntityList.hpp"
#include "Modules/Console.hpp"
#include "Modules/Engine.hpp"
#include "Modules/Server.hpp"
#include "Offsets.hpp"
#include "Utils/Memory.hpp"
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
  resolvedClasses.resize(classes.size());
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
  resolvedClasses.clear();

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

  // Track which classnames we've already SendTable-discovered to avoid
  // redundant walks (many entities share the same class).
  std::unordered_map<std::string, bool> sendTableDiscovered;

  for (int i = 0; i < Offsets::NUM_ENT_ENTRIES; ++i) {
    auto info = entityList->GetEntityInfoByIndex(i);
    if (!info || !info->m_pEntity) continue;

    auto ent = info->m_pEntity;
    const char* className = server->GetEntityClassName(ent);
    if (!className) continue;

    RegisterClassSchema(className);

    // Phase 4: Get this entity's ServerClass via vtable and discover
    // its SendTable fields, associating them with the entity classname.
    if (!sendTableDiscovered[className]) {
      sendTableDiscovered[className] = true;
      ServerClass* sc = Memory::VMT<ServerClass*(__rescall*)(void*)>(
          ent, Offsets::GetServerClass)(ent);
      if (sc && sc->m_pTable) {
        DiscoverSendTableFields(className, sc->m_pTable);
      }
    }
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
  }
}

const HdemFieldDef* EntitySnapshotter::GetFieldDef(uint16_t fieldId) const {
  if (fieldId < allFields.size()) {
    return &allFields[fieldId];
  }
  return nullptr;
}

// Max fields per class to bound per-tick work.
// TODO: explore raising this if important fields get truncated.
static constexpr int MAX_FIELDS_PER_CLASS = 64;

static HdemFieldType SendPropTypeToHdem(SendPropType t) {
  switch (t) {
    case DPT_Int:
      return HDEM_INT32;
    case DPT_Float:
      return HDEM_FLOAT;
    case DPT_Vector:
      return HDEM_VEC3;
    case DPT_VectorXY:
      return HDEM_VEC3;
    default:
      return static_cast<HdemFieldType>(0xFF);  // sentinel: skip
  }
}

void EntitySnapshotter::DiscoverSendTableFields(const std::string& className,
                                                SendTable* table) {
  uint16_t cid = GetOrAddClass(className);
  auto& cls = classes[cid];

  // Recursive SendTable walker — collects leaf props
  struct PropCollector {
    std::vector<std::pair<std::string, HdemFieldType>> props;
    std::unordered_map<std::string, bool> seen;

    void Walk(SendTable* tbl) {
      if (!tbl) return;
      for (int i = 0; i < tbl->m_nProps; ++i) {
        SendProp* prop = &tbl->m_pProps[i];
        if (!prop->m_pVarName) continue;

        std::string name(prop->m_pVarName);

        // Skip engine pseudo-prop
        if (name == "baseclass") continue;

        if (prop->m_Type == DPT_DataTable) {
          // Recurse into sub-tables
          if (prop->m_pDataTable) {
            Walk(prop->m_pDataTable);
          }
          continue;
        }

        // Skip unsupported types (DPT_Array, DPT_Int64)
        HdemFieldType htype = SendPropTypeToHdem(prop->m_Type);
        if (htype == static_cast<HdemFieldType>(0xFF)) continue;

        // Deduplicate (sub-tables can re-expose same prop name)
        if (seen.count(name)) continue;
        seen[name] = true;

        props.push_back({name, htype});
      }
    }
  };

  PropCollector collector;
  collector.Walk(table);

  // Build a set of field names already registered for this class
  std::unordered_map<std::string, bool> existingFields;
  for (const auto& f : cls.fields) {
    existingFields[f.name] = true;
  }

  int added = 0;
  for (const auto& [propName, htype] : collector.props) {
    if ((int)cls.fields.size() >= MAX_FIELDS_PER_CLASS) break;
    if (existingFields.count(propName)) continue;

    uint16_t fid = GetOrAddField(propName, htype);
    cls.fields.push_back(allFields[fid]);
    added++;
  }

  if (added > 0) {
    console->Print("HDEM: %s (%s) -> %d fields (%d from SendTable)\n",
                   className.c_str(), table->m_pNetTableName,
                   (int)cls.fields.size(), added);
  }
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
      RegisterClassSchema(classNameCStr);
      ServerClass* sc = Memory::VMT<ServerClass*(__rescall*)(void*)>(
          ent, Offsets::GetServerClass)(ent);
      if (sc && sc->m_pTable) {
        DiscoverSendTableFields(classNameCStr, sc->m_pTable);
      }
      itClass = classNameToId.find(classNameCStr);
      if (itClass == classNameToId.end()) {
        continue;
      }
    }
    uint16_t classId = itClass->second;
    const auto& cls = classes[classId];

    // Lazy resolve fields
    auto& rcls = resolvedClasses[classId];
    if (!rcls.resolved) {
      rcls.fields.clear();
      for (const auto& field : cls.fields) {
        ResolvedField rf;
        rf.fieldId = field.fieldId;
        rf.offset = 0;
        rf.type = EntField::Type::NONE;
        rf.size = HdemFieldSize(field.type);

        if (field.fieldId != HDEM_FIELD_ORIGIN &&
            field.fieldId != HDEM_FIELD_ANGLES &&
            field.fieldId != HDEM_FIELD_VELOCITY &&
            field.fieldId != HDEM_FIELD_SOLIDTYPE &&
            field.fieldId != HDEM_FIELD_MINS &&
            field.fieldId != HDEM_FIELD_MAXS) {
          try {
            auto val = EntField::getServerOffset(ent, field.name.c_str());
            rf.offset = val.first;
            rf.type = val.second;
          } catch (...) {
          }
        }
        rcls.fields.push_back(rf);
      }
      rcls.resolved = true;
    }

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

    // Populate all field values as raw bytes using cached offsets
    for (const auto& rf : rcls.fields) {
      if (rf.fieldId == HDEM_FIELD_ORIGIN) {
        tracked.fieldValues[rf.fieldId] = ValueToBytes(tracked.position);
      } else if (rf.fieldId == HDEM_FIELD_ANGLES) {
        tracked.fieldValues[rf.fieldId] = ValueToBytes(tracked.angles);
      } else if (rf.fieldId == HDEM_FIELD_VELOCITY) {
        tracked.fieldValues[rf.fieldId] = ValueToBytes(tracked.velocity);
      } else if (rf.fieldId == HDEM_FIELD_SOLIDTYPE) {
        uint8_t solid = SOLID_NONE;
        try {
          ICollideable* coll = &se->collision();
          if (coll) solid = static_cast<uint8_t>(coll->GetSolid());
        } catch (...) {
        }
        tracked.fieldValues[rf.fieldId] = ValueToBytes(solid);
      } else if (rf.fieldId == HDEM_FIELD_MINS) {
        Vector mins(0, 0, 0);
        try {
          ICollideable* coll = &se->collision();
          if (coll) mins = coll->OBBMins();
        } catch (...) {
        }
        tracked.fieldValues[rf.fieldId] = ValueToBytes(mins);
      } else if (rf.fieldId == HDEM_FIELD_MAXS) {
        Vector maxs(0, 0, 0);
        try {
          ICollideable* coll = &se->collision();
          if (coll) maxs = coll->OBBMaxs();
        } catch (...) {
        }
        tracked.fieldValues[rf.fieldId] = ValueToBytes(maxs);
      } else {
        if (rf.offset != 0 && rf.type != EntField::Type::NONE) {
          if (rf.fieldId == HDEM_FIELD_TOGGLESTATE &&
              rf.type == EntField::Type::CHAR) {
            int intVal = *(char*)((uintptr_t)se + rf.offset);
            tracked.fieldValues[rf.fieldId] = ValueToBytes(intVal);
          } else {
            std::vector<uint8_t> buf(rf.size);
            std::memcpy(buf.data(), (void*)((uintptr_t)se + rf.offset),
                        rf.size);
            tracked.fieldValues[rf.fieldId] = buf;
          }
        } else {
          tracked.fieldValues[rf.fieldId] = std::vector<uint8_t>(rf.size, 0);
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

void EntitySnapshotter::GetSnapshotAndSchema(
    std::vector<TrackedEntity>& outEntities, int& outTick,
    std::vector<HdemClassDef>& outClasses,
    std::vector<HdemFieldDef>& outFields) {
  std::lock_guard<std::mutex> lock(mutex);
  outTick = currentTick;
  outEntities.clear();
  outEntities.reserve(activeEntities.size());
  for (const auto& pair : activeEntities) {
    outEntities.push_back(pair.second);
  }
  outClasses = classes;
  outFields = allFields;
}
