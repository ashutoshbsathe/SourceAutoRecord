#include "HdemReader.hpp"
#include <cstring>
#include "EntitySnapshotter.hpp"

HdemReader::HdemReader() {}

HdemReader::~HdemReader() {
  Close();
}

bool HdemReader::Open(const std::string& path) {
  file.open(path, std::ios::binary | std::ios::in);
  if (!file.is_open()) return false;

  uint32_t magic = 0;
  uint16_t version = 0;
  uint16_t flags = 0;

  file.read(reinterpret_cast<char*>(&magic), 4);
  file.read(reinterpret_cast<char*>(&version), 2);
  file.read(reinterpret_cast<char*>(&flags), 2);

  if (file.gcount() < 2 || magic != HDEM_MAGIC) {
    file.close();
    return false;
  }

  mapName = ReadString();
  file.read(reinterpret_cast<char*>(&tickrate), 4);
  file.read(reinterpret_cast<char*>(&timestamp), 8);
  sarVersion = ReadString();
  gameDir = ReadString();

  file.read(reinterpret_cast<char*>(&schemaOffset), 8);

  if (file.gcount() < 8) {
    file.close();
    return false;
  }

  uint64_t ticksStartPos = file.tellg();

  // Load trailing schema tables
  if (schemaOffset != 0) {
    file.seekg(schemaOffset);

    // Read Class Table
    uint16_t numClasses = 0;
    file.read(reinterpret_cast<char*>(&numClasses), 2);
    for (uint16_t i = 0; i < numClasses; ++i) {
      uint16_t cid = 0;
      file.read(reinterpret_cast<char*>(&cid), 2);
      std::string cname = ReadString();
      classes[cid] = {cid, cname};
    }

    // Read Field Table
    uint16_t numFields = 0;
    file.read(reinterpret_cast<char*>(&numFields), 2);
    for (uint16_t i = 0; i < numFields; ++i) {
      uint16_t fid = 0;
      file.read(reinterpret_cast<char*>(&fid), 2);
      std::string fname = ReadString();
      uint8_t ftype = 0;
      file.read(reinterpret_cast<char*>(&ftype), 1);
      fields[fid] = {fid, fname, static_cast<HdemFieldType>(ftype)};
    }
  }

  // Seek back to start of ticks
  file.seekg(ticksStartPos);
  reachedEof = false;
  currentTick = -1;
  currentEntities.clear();

  return true;
}

void HdemReader::Close() {
  if (file.is_open()) {
    file.close();
  }
  classes.clear();
  fields.clear();
  currentEntities.clear();
  currentTick = -1;
  reachedEof = false;
}

std::string HdemReader::ReadString() {
  std::string str;
  char ch;
  while (file.get(ch)) {
    if (ch == '\0') break;
    str.push_back(ch);
  }
  return str;
}

bool HdemReader::ReadNextFrame() {
  if (reachedEof) return false;

  int32_t tickNumber = 0;
  uint16_t numEntities = 0;
  uint32_t frameByteSize = 0;

  file.read(reinterpret_cast<char*>(&tickNumber), 4);
  if (file.gcount() < 4) {
    reachedEof = true;
    return false;
  }
  file.read(reinterpret_cast<char*>(&numEntities), 2);
  file.read(reinterpret_cast<char*>(&frameByteSize), 4);

  currentTick = tickNumber;

  for (uint16_t e = 0; e < numEntities; ++e) {
    uint16_t entIndex = 0;
    uint16_t serialNumber = 0;
    uint16_t classId = 0;
    uint8_t flags = 0;
    uint8_t numFields = 0;

    file.read(reinterpret_cast<char*>(&entIndex), 2);
    file.read(reinterpret_cast<char*>(&serialNumber), 2);
    file.read(reinterpret_cast<char*>(&classId), 2);
    file.read(reinterpret_cast<char*>(&flags), 1);
    file.read(reinterpret_cast<char*>(&numFields), 1);

    if (flags & HDEM_ENT_DELETED) {
      currentEntities.erase(entIndex);
      continue;
    }

    auto& entState = currentEntities[entIndex];
    entState.set_entity_index(entIndex);
    entState.set_serial_number(serialNumber);

    auto itClass = classes.find(classId);
    if (itClass != classes.end()) {
      entState.set_class_name(itClass->second.name);
    }

    for (uint8_t f = 0; f < numFields; ++f) {
      uint16_t fid = 0;
      file.read(reinterpret_cast<char*>(&fid), 2);

      if (fid == HDEM_FIELD_CLASSNAME) {
        entState.set_class_name(ReadString());
      } else if (fid == HDEM_FIELD_NAME) {
        entState.set_target_name(ReadString());
      } else if (fid == HDEM_FIELD_ORIGIN) {
        float x = 0.0f, y = 0.0f, z = 0.0f;
        file.read(reinterpret_cast<char*>(&x), 4);
        file.read(reinterpret_cast<char*>(&y), 4);
        file.read(reinterpret_cast<char*>(&z), 4);
        entState.mutable_position()->set_x(x);
        entState.mutable_position()->set_y(y);
        entState.mutable_position()->set_z(z);
      } else if (fid == HDEM_FIELD_ANGLES) {
        float x = 0.0f, y = 0.0f, z = 0.0f;
        file.read(reinterpret_cast<char*>(&x), 4);
        file.read(reinterpret_cast<char*>(&y), 4);
        file.read(reinterpret_cast<char*>(&z), 4);
        entState.mutable_angles()->set_x(x);
        entState.mutable_angles()->set_y(y);
        entState.mutable_angles()->set_z(z);
      } else if (fid == HDEM_FIELD_VELOCITY) {
        float x = 0.0f, y = 0.0f, z = 0.0f;
        file.read(reinterpret_cast<char*>(&x), 4);
        file.read(reinterpret_cast<char*>(&y), 4);
        file.read(reinterpret_cast<char*>(&z), 4);
        entState.mutable_velocity()->set_x(x);
        entState.mutable_velocity()->set_y(y);
        entState.mutable_velocity()->set_z(z);
      } else {
        auto itField = fields.find(fid);
        if (itField != fields.end()) {
          const auto& fdef = itField->second;
          portal2_harness::EntityField* fproto = nullptr;
          for (int idx = 0; idx < entState.fields_size(); ++idx) {
            if (entState.fields(idx).name() == fdef.name) {
              fproto = entState.mutable_fields(idx);
              break;
            }
          }
          if (!fproto) {
            fproto = entState.add_fields();
            fproto->set_name(fdef.name);
          }

          switch (fdef.type) {
            case HDEM_FLOAT: {
              float val = 0.0f;
              file.read(reinterpret_cast<char*>(&val), 4);
              fproto->set_float_val(val);
              break;
            }
            case HDEM_INT32: {
              int32_t val = 0;
              file.read(reinterpret_cast<char*>(&val), 4);
              fproto->set_int_val(val);
              break;
            }
            case HDEM_VEC3: {
              float x = 0.0f, y = 0.0f, z = 0.0f;
              file.read(reinterpret_cast<char*>(&x), 4);
              file.read(reinterpret_cast<char*>(&y), 4);
              file.read(reinterpret_cast<char*>(&z), 4);
              fproto->mutable_vec3_val()->set_x(x);
              fproto->mutable_vec3_val()->set_y(y);
              fproto->mutable_vec3_val()->set_z(z);
              break;
            }
            case HDEM_BOOL: {
              bool val = false;
              file.read(reinterpret_cast<char*>(&val), 1);
              fproto->set_bool_val(val);
              break;
            }
            case HDEM_STRING: {
              fproto->set_string_val(ReadString());
              break;
            }
            case HDEM_HANDLE: {
              int32_t val = 0;
              file.read(reinterpret_cast<char*>(&val), 4);
              fproto->set_handle_val(val);
              break;
            }
            case HDEM_BYTE: {
              uint8_t val = 0;
              file.read(reinterpret_cast<char*>(&val), 1);
              fproto->set_int_val(val);
              break;
            }
            case HDEM_SHORT: {
              int16_t val = 0;
              file.read(reinterpret_cast<char*>(&val), 2);
              fproto->set_int_val(val);
              break;
            }
            case HDEM_COLOR: {
              uint8_t r = 0, g = 0, b = 0, a = 0;
              file.read(reinterpret_cast<char*>(&r), 1);
              file.read(reinterpret_cast<char*>(&g), 1);
              file.read(reinterpret_cast<char*>(&b), 1);
              file.read(reinterpret_cast<char*>(&a), 1);
              fproto->set_int_val((r << 24) | (g << 16) | (b << 8) | a);
              break;
            }
            default:
              break;
          }
        }
      }
    }
  }

  return true;
}

bool HdemReader::AdvanceToTick(int targetTick) {
  if (!IsOpen() || reachedEof) return false;

  while (!reachedEof && currentTick < targetTick) {
    // Peek the next frame's tick to see if it exceeds targetTick
    std::streampos pos = file.tellg();
    int32_t nextTick = 0;
    file.read(reinterpret_cast<char*>(&nextTick), 4);
    if (file.gcount() < 4) {
      reachedEof = true;
      break;
    }
    file.seekg(pos);

    if (nextTick > targetTick) {
      break;
    }

    if (!ReadNextFrame()) {
      break;
    }
  }
  return true;
}

void HdemReader::GetSnapshot(portal2_harness::EntitySnapshot* outSnapshot, int tick) {
  outSnapshot->Clear();
  outSnapshot->set_tick(tick);
  outSnapshot->set_is_full_snapshot(true);

  for (const auto& pair : currentEntities) {
    outSnapshot->add_entities()->CopyFrom(pair.second);
  }
}
