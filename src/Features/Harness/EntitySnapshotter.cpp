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

EntitySnapshotter::EntitySnapshotter() {
  if (entityList && Offsets::NUM_ENT_ENTRIES > 0) {
    slots.resize(Offsets::NUM_ENT_ENTRIES);
  }
}

uint16_t EntitySnapshotter::GetOrAddClass(const std::string& className) {
  auto it = classNameToId.find(className);
  if (it != classNameToId.end()) return it->second;
  uint16_t id = static_cast<uint16_t>(classes.size());
  classNameToId[className] = id;
  classes.push_back({id, className, {}});
  classLayouts.resize(classes.size());
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
  classLayouts.clear();

  if (slots.size() != (size_t)Offsets::NUM_ENT_ENTRIES) {
    slots.resize(Offsets::NUM_ENT_ENTRIES);
  }
  for (auto& slot : slots) {
    slot.alive = false;
  }

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
      {HDEM_FIELD_DISABLED, "m_bDisabled", HDEM_BOOL},
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

    if (!sendTableDiscovered[className]) {
      sendTableDiscovered[className] = true;
      ServerClass* sc = Memory::VMT<ServerClass*(__rescall*)(void*)>(
          ent, Offsets::GetServerClass)(ent);
      if (sc && sc->m_pTable) {
        DiscoverSendTableFields(className, sc->m_pTable);
      }
    }
  }

  // The SendTable walk only sees networked props; several puzzle status fields
  // are datamap-only, so patch them in by hand (see status_field_recon.md).
  RegisterCuratedStatusFields();

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

// Curated datamap-only ([dm]) status fields the SendTable walk never registers
// (it sees networked props only). The read path EntField::getServerOffset
// resolves datamaps, so registration is the only gap. Keep this list small.
namespace {
struct CuratedStatusField {
  const char* className;
  const char* fieldName;
  HdemFieldType type;
};

const CuratedStatusField kCuratedStatusFields[] = {
    {"prop_weighted_cube", "m_nCubeType",
     HDEM_INT32},  // 0=standard, 2=reflective
    {"prop_weighted_cube", "m_bActivated", HDEM_BOOL},  // pressing a button
    {"point_laser_target", "m_bPowered", HDEM_BOOL},    // catcher/relay sensor
    {"trigger_catapult", "m_bDisabled",
     HDEM_BOOL},  // faith plate: false=active
};
}  // namespace

void EntitySnapshotter::RegisterCuratedStatusFields() {
  for (const auto& curated : kCuratedStatusFields) {
    // Ensure the class carries the universal fields even if it was absent from
    // the first map (schema discovery runs once per process).
    RegisterClassSchema(curated.className);
    auto& cls = classes[GetOrAddClass(curated.className)];

    bool present = false;
    for (const auto& f : cls.fields) {
      if (f.name == curated.fieldName) {
        present = true;
        break;
      }
    }
    if (present) continue;

    uint16_t fid = GetOrAddField(curated.fieldName, curated.type);
    cls.fields.push_back(allFields[fid]);
  }
}

const HdemFieldDef* EntitySnapshotter::GetFieldDef(uint16_t fieldId) const {
  if (fieldId < allFields.size()) {
    return &allFields[fieldId];
  }
  return nullptr;
}

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

  struct PropCollector {
    std::vector<std::pair<std::string, HdemFieldType>> props;
    std::unordered_map<std::string, bool> seen;

    void Walk(SendTable* tbl) {
      if (!tbl) return;
      for (int i = 0; i < tbl->m_nProps; ++i) {
        SendProp* prop = &tbl->m_pProps[i];
        if (!prop->m_pVarName) continue;

        std::string name(prop->m_pVarName);
        if (name == "baseclass") continue;

        if (prop->m_Type == DPT_DataTable) {
          if (prop->m_pDataTable) {
            Walk(prop->m_pDataTable);
          }
          continue;
        }

        HdemFieldType htype = SendPropTypeToHdem(prop->m_Type);
        if (htype == static_cast<HdemFieldType>(0xFF)) continue;

        if (seen.count(name)) continue;
        seen[name] = true;

        props.push_back({name, htype});
      }
    }
  };

  PropCollector collector;
  collector.Walk(table);

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

void EntitySnapshotter::InitSlot(int index, CEntInfo* info, void* entity) {
  auto& slot = slots[index];
  slot.alive = true;
  slot.serial = static_cast<uint16_t>(info->m_SerialNumber);

  const char* className = server->GetEntityClassName(entity);
  slot.className = className ? className : "";
  const char* targetName = server->GetEntityName(entity);
  slot.targetName = targetName ? targetName : "";

  uint16_t cid = GetOrAddClass(slot.className);
  slot.classId = cid;

  auto& layout = classLayouts[cid];
  if (layout.fields.empty() && !classes[cid].fields.empty()) {
    layout.classId = cid;
    layout.fieldBufSize = 0;

    for (const auto& field : classes[cid].fields) {
      FieldSlot fs;
      fs.fieldId = field.fieldId;
      fs.dstOffset = layout.fieldBufSize;
      fs.srcOffset = 0;
      fs.size = HdemFieldSize(field.type);

      if (field.fieldId == HDEM_FIELD_ORIGIN) {
        fs.mode = ReadMode::AbsOrigin;
      } else if (field.fieldId == HDEM_FIELD_ANGLES) {
        fs.mode = ReadMode::AbsAngles;
      } else if (field.fieldId == HDEM_FIELD_VELOCITY) {
        fs.mode = ReadMode::AbsVelocity;
      } else if (field.fieldId == HDEM_FIELD_SOLIDTYPE) {
        fs.mode = ReadMode::CollisionSolid;
      } else if (field.fieldId == HDEM_FIELD_MINS) {
        fs.mode = ReadMode::CollisionMins;
      } else if (field.fieldId == HDEM_FIELD_MAXS) {
        fs.mode = ReadMode::CollisionMaxs;
      } else {
        fs.mode = ReadMode::DirectOffset;
        auto val = EntField::getServerOffset(entity, field.name.c_str());
        fs.srcOffset = val.first;
        if (val.first == 0 || val.second == EntField::Type::NONE) {
          fs.srcOffset = 0;
        }
      }

      layout.fields.push_back(fs);
      layout.fieldBufSize += fs.size;
    }
  }

  slot.fieldBufSize = layout.fieldBufSize;
  slot.fieldBuf = std::make_unique<uint8_t[]>(slot.fieldBufSize);
  std::memset(slot.fieldBuf.get(), 0, slot.fieldBufSize);
  slot.changeVersion = 1;
}

static void ReadFields(EntitySlot& slot, ServerEnt* se,
                       const ClassLayout& layout, uint8_t* dstBuf) {
  for (const auto& fs : layout.fields) {
    uint8_t* dst = dstBuf + fs.dstOffset;
    switch (fs.mode) {
      case ReadMode::AbsOrigin: {
        Vector v = se->abs_origin();
        std::memcpy(dst, &v, 12);
        break;
      }
      case ReadMode::AbsAngles: {
        QAngle v = se->abs_angles();
        std::memcpy(dst, &v, 12);
        break;
      }
      case ReadMode::AbsVelocity: {
        Vector v = se->abs_velocity();
        std::memcpy(dst, &v, 12);
        break;
      }
      case ReadMode::CollisionSolid: {
        uint8_t solid = SOLID_NONE;
        ICollideable* coll = &se->collision();
        if (coll) solid = static_cast<uint8_t>(coll->GetSolid());
        *dst = solid;
        break;
      }
      case ReadMode::CollisionMins: {
        Vector mins(0, 0, 0);
        ICollideable* coll = &se->collision();
        if (coll) mins = coll->OBBMins();
        std::memcpy(dst, &mins, 12);
        break;
      }
      case ReadMode::CollisionMaxs: {
        Vector maxs(0, 0, 0);
        ICollideable* coll = &se->collision();
        if (coll) maxs = coll->OBBMaxs();
        std::memcpy(dst, &maxs, 12);
        break;
      }
      case ReadMode::DirectOffset: {
        if (fs.srcOffset != 0) {
          if (fs.fieldId == HDEM_FIELD_TOGGLESTATE && fs.size == 4) {
            int intVal = *(char*)((uintptr_t)se + fs.srcOffset);
            std::memcpy(dst, &intVal, 4);
          } else {
            std::memcpy(dst, (const char*)se + fs.srcOffset, fs.size);
          }
        } else {
          std::memset(dst, 0, fs.size);
        }
        break;
      }
    }
  }
}

void EntitySnapshotter::Update() {
  if (!server || !entityList) return;

  std::lock_guard<std::mutex> lock(mutex);
  currentTick = server->gpGlobals->tickcount;

  if (slots.size() != (size_t)Offsets::NUM_ENT_ENTRIES) {
    slots.resize(Offsets::NUM_ENT_ENTRIES);
  }

  for (int i = 0; i < Offsets::NUM_ENT_ENTRIES; ++i) {
    auto info = entityList->GetEntityInfoByIndex(i);
    auto& slot = slots[i];

    if (!info || !info->m_pEntity) {
      slot.alive = false;
      continue;
    }

    auto ent = info->m_pEntity;
    uint16_t serial = static_cast<uint16_t>(info->m_SerialNumber);

    if (!slot.alive || slot.serial != serial) {
      InitSlot(i, info, ent);
    }

    const auto& layout = classLayouts[slot.classId];

    if (scratchBuf.size() < slot.fieldBufSize) {
      scratchBuf.resize(slot.fieldBufSize);
    }

    std::memcpy(scratchBuf.data(), slot.fieldBuf.get(), slot.fieldBufSize);
    ReadFields(slot, SE(ent), layout, slot.fieldBuf.get());

    if (std::memcmp(scratchBuf.data(), slot.fieldBuf.get(),
                    slot.fieldBufSize) != 0) {
      slot.changeVersion++;
    }
  }
}
