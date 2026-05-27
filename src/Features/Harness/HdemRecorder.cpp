#include "HdemRecorder.hpp"

#include <cstring>
#include <ctime>

#include "EntitySnapshotter.hpp"
#include "Harness.hpp"
#include "Modules/Engine.hpp"
#include "Modules/Server.hpp"
#include "Version.hpp"

HdemRecorder::~HdemRecorder() { Stop(); }

template <typename T>
static inline void WritePOD(std::ofstream& out, const T& val) {
  out.write(reinterpret_cast<const char*>(&val), sizeof(T));
}

static inline void WriteString(std::ofstream& out, const std::string& str) {
  out.write(str.c_str(), str.size() + 1);
}

void HdemRecorder::WriteHeader(const std::string& mapName, float tickrate) {
  WritePOD(file, HDEM_MAGIC);
  WritePOD(file, HDEM_VERSION);
  WritePOD(file, uint16_t(0));  // Flags

  WriteString(file, mapName);
  WritePOD(file, tickrate);
  WritePOD(file, static_cast<uint64_t>(time(nullptr)));
  WriteString(file, SAR_VERSION);
  WriteString(file, engine ? engine->GetGameDirectory() : "");

  // Write placeholder for trailing schema offset
  schemaOffsetPos = file.tellp();
  WritePOD(file, uint64_t(0));

  headerWritten = true;
  totalBytes = file.tellp();
}

void HdemRecorder::WriteClassTable() {
  if (!harness || !harness->entitySnapshotter) {
    WritePOD(file, uint16_t(0));
    return;
  }
  const auto& classes = harness->entitySnapshotter->GetClasses();
  WritePOD(file, static_cast<uint16_t>(classes.size()));
  for (const auto& cls : classes) {
    WritePOD(file, cls.classId);
    WriteString(file, cls.name);
  }
}

void HdemRecorder::WriteFieldTable() {
  if (!harness || !harness->entitySnapshotter) {
    WritePOD(file, uint16_t(0));
    return;
  }
  const auto& allFields = harness->entitySnapshotter->GetFields();
  WritePOD(file, static_cast<uint16_t>(allFields.size()));
  for (const auto& field : allFields) {
    WritePOD(file, field.fieldId);
    WriteString(file, field.name);
    WritePOD(file, static_cast<uint8_t>(field.type));
  }
}

bool HdemRecorder::Start(const std::string& path, const std::string& mapName,
                         float tickrate) {
  if (isActive) Stop();

  file.open(path, std::ios::binary | std::ios::out);
  if (!file.is_open()) return false;

  isActive = true;
  headerWritten = false;
  totalBytes = 0;
  totalTicks = 0;

  lastEntityState.clear();
  lastEntitySerial.clear();

  if (harness && harness->entitySnapshotter) {
    harness->entitySnapshotter->DiscoverSchema();
  }
  WriteHeader(mapName, tickrate);
  return true;
}

void HdemRecorder::Stop() {
  if (!isActive) return;

  // Append trailing schema tables
  uint64_t actualSchemaOffset = static_cast<uint64_t>(file.tellp());
  WriteClassTable();
  WriteFieldTable();

  // Write footer
  WritePOD(file, static_cast<uint32_t>(totalTicks));
  WritePOD(file, static_cast<uint32_t>(lastEntitySerial.size()));
  WritePOD(file, uint32_t(0));  // Checksum

  // Backfill real schema offset into header
  file.seekp(schemaOffsetPos);
  WritePOD(file, actualSchemaOffset);

  file.close();
  isActive = false;
  headerWritten = false;
}

template <typename T>
static inline void AppendToBuffer(std::vector<uint8_t>& buf, const T& val) {
  const uint8_t* ptr = reinterpret_cast<const uint8_t*>(&val);
  buf.insert(buf.end(), ptr, ptr + sizeof(T));
}

void HdemRecorder::RecordTick(int tickNumber) {
  if (!isActive || !headerWritten || !server || !harness ||
      !harness->entitySnapshotter)
    return;

  std::vector<TrackedEntity> currentEntities;
  int currentTick;
  harness->entitySnapshotter->GetSnapshot(currentEntities, currentTick);

  tickBuffer.clear();
  uint16_t numEntitiesWritten = 0;

  std::unordered_map<int, uint16_t> currentSerials;
  for (const auto& ent : currentEntities) {
    currentSerials[ent.entityIndex] = ent.serialNumber;
  }

  // Detect and write records for deleted entities
  for (auto it = lastEntitySerial.begin(); it != lastEntitySerial.end();) {
    int i = it->first;
    uint16_t oldSerial = it->second;

    auto itCurrent = currentSerials.find(i);
    bool stillExists =
        (itCurrent != currentSerials.end() && itCurrent->second == oldSerial);

    if (!stillExists) {
      AppendToBuffer(tickBuffer, static_cast<uint16_t>(i));
      AppendToBuffer(tickBuffer, oldSerial);
      AppendToBuffer(tickBuffer, uint16_t(0));  // classId
      AppendToBuffer(tickBuffer, static_cast<uint8_t>(HDEM_ENT_DELETED));
      AppendToBuffer(tickBuffer, uint8_t(0));  // fieldsWritten

      numEntitiesWritten++;

      lastEntityState.erase(i);
      it = lastEntitySerial.erase(it);
    } else {
      ++it;
    }
  }

  const auto& classes = harness->entitySnapshotter->GetClasses();

  for (const auto& ent : currentEntities) {
    uint16_t classId = ent.classId;
    if (classId >= classes.size()) continue;
    const auto& cls = classes[classId];

    static std::vector<uint8_t> currentRawState;
    currentRawState.clear();

    for (const auto& field : cls.fields) {
      auto itVal = ent.fieldValues.find(field.fieldId);
      if (itVal != ent.fieldValues.end()) {
        currentRawState.insert(currentRawState.end(), itVal->second.begin(),
                               itVal->second.end());
      } else {
        size_t fsize = HdemFieldSize(field.type);
        currentRawState.insert(currentRawState.end(), fsize, 0);
      }
    }

    uint16_t serialNum = ent.serialNumber;
    bool isNewOrModified = false;
    bool isFullSnapshot = false;

    int i = ent.entityIndex;
    auto itLastState = lastEntityState.find(i);
    auto itLastSerial = lastEntitySerial.find(i);

    if (itLastState == lastEntityState.end() ||
        itLastSerial == lastEntitySerial.end() ||
        itLastSerial->second != serialNum) {
      isNewOrModified = true;
      isFullSnapshot = true;
    } else {
      if (currentRawState != itLastState->second) {
        isNewOrModified = true;
      }
    }

    if (!isNewOrModified) continue;

    AppendToBuffer(tickBuffer, static_cast<uint16_t>(i));
    AppendToBuffer(tickBuffer, serialNum);
    AppendToBuffer(tickBuffer, classId);

    uint8_t flags = HDEM_ENT_ALIVE;
    if (isFullSnapshot) flags |= HDEM_ENT_FULL_SNAPSHOT;
    AppendToBuffer(tickBuffer, flags);

    size_t numFieldsOffset = tickBuffer.size();
    AppendToBuffer(tickBuffer, uint8_t(0));

    uint8_t fieldsWritten = 0;
    size_t byteOffset = 0;

    for (const auto& field : cls.fields) {
      size_t fsize = HdemFieldSize(field.type);
      bool writeField = isFullSnapshot;

      if (!isFullSnapshot) {
        if (byteOffset + fsize <= itLastState->second.size() &&
            byteOffset + fsize <= currentRawState.size()) {
          if (std::memcmp(&currentRawState[byteOffset],
                          &itLastState->second[byteOffset], fsize) != 0) {
            writeField = true;
          }
        } else {
          writeField = true;
        }
      }

      if (writeField && byteOffset + fsize <= currentRawState.size()) {
        AppendToBuffer(tickBuffer, field.fieldId);
        tickBuffer.insert(tickBuffer.end(), &currentRawState[byteOffset],
                          &currentRawState[byteOffset] + fsize);
        fieldsWritten++;
      }

      byteOffset += fsize;
    }

    if (isFullSnapshot) {
      // Write classname
      AppendToBuffer(tickBuffer, static_cast<uint16_t>(HDEM_FIELD_CLASSNAME));
      const std::string& cname = ent.className;
      tickBuffer.insert(tickBuffer.end(), cname.c_str(),
                        cname.c_str() + cname.size() + 1);
      fieldsWritten++;

      // Write targetname
      AppendToBuffer(tickBuffer, static_cast<uint16_t>(HDEM_FIELD_NAME));
      const std::string& tname = ent.targetName;
      tickBuffer.insert(tickBuffer.end(), tname.c_str(),
                        tname.c_str() + tname.size() + 1);
      fieldsWritten++;
    }

    tickBuffer[numFieldsOffset] = fieldsWritten;

    lastEntityState[i] = currentRawState;
    lastEntitySerial[i] = serialNum;
    numEntitiesWritten++;
  }

  uint32_t frameByteSize = static_cast<uint32_t>(tickBuffer.size());
  WritePOD(file, static_cast<int32_t>(tickNumber));
  WritePOD(file, numEntitiesWritten);
  WritePOD(file, frameByteSize);

  if (frameByteSize > 0) {
    file.write(reinterpret_cast<const char*>(tickBuffer.data()), frameByteSize);
  }

  totalTicks++;
  totalBytes +=
      (sizeof(int32_t) + sizeof(uint16_t) + sizeof(uint32_t) + frameByteSize);
}
