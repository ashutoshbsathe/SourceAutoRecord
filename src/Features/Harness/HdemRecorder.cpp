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

  lastSeenVersion.assign(Offsets::NUM_ENT_ENTRIES, 0);
  lastSeenSerial.assign(Offsets::NUM_ENT_ENTRIES, 0);
  lastEntityState.clear();
  lastEntityState.resize(Offsets::NUM_ENT_ENTRIES);
  lastRecordedTick = -1;

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

  uint32_t activeCount = 0;
  for (auto s : lastSeenSerial) {
    if (s != 0) activeCount++;
  }
  WritePOD(file, activeCount);
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

  if (tickNumber == lastRecordedTick) return;
  lastRecordedTick = tickNumber;

  tickBuffer.clear();
  uint16_t numEntitiesWritten = 0;

  for (int i = 0; i < Offsets::NUM_ENT_ENTRIES; ++i) {
    const auto& slot = harness->entitySnapshotter->GetSlot(i);
    uint16_t oldSerial = lastSeenSerial[i];
    bool wasSeen = (oldSerial != 0);
    bool isAlive = slot.alive;

    // Detect and write records for deleted/reused entities
    if (wasSeen && (!isAlive || slot.serial != oldSerial)) {
      AppendToBuffer(tickBuffer, static_cast<uint16_t>(i));
      AppendToBuffer(tickBuffer, oldSerial);
      AppendToBuffer(tickBuffer, uint16_t(0));  // classId
      AppendToBuffer(tickBuffer, static_cast<uint8_t>(HDEM_ENT_DELETED));
      AppendToBuffer(tickBuffer, uint8_t(0));  // fieldsWritten

      numEntitiesWritten++;

      lastSeenSerial[i] = 0;
      lastSeenVersion[i] = 0;
      lastEntityState[i].clear();
      wasSeen = false;
    }

    // Detect and write records for new/modified entities
    if (isAlive) {
      uint32_t currentVersion = slot.changeVersion;
      if (!wasSeen || currentVersion != lastSeenVersion[i]) {
        bool isFullSnapshot = !wasSeen;

        AppendToBuffer(tickBuffer, static_cast<uint16_t>(i));
        AppendToBuffer(tickBuffer, slot.serial);
        AppendToBuffer(tickBuffer, slot.classId);

        uint8_t flags = HDEM_ENT_ALIVE;
        if (isFullSnapshot) flags |= HDEM_ENT_FULL_SNAPSHOT;
        AppendToBuffer(tickBuffer, flags);

        size_t numFieldsOffset = tickBuffer.size();
        AppendToBuffer(tickBuffer, uint8_t(0));

        uint8_t fieldsWritten = 0;
        const auto& layout =
            harness->entitySnapshotter->GetClassLayout(slot.classId);

        if (isFullSnapshot) {
          lastEntityState[i].assign(slot.fieldBufSize, 0);
        }

        const uint8_t* currentBuf = slot.fieldBuf.get();

        for (const auto& fs : layout.fields) {
          bool writeField = isFullSnapshot;

          if (!isFullSnapshot) {
            if (std::memcmp(currentBuf + fs.dstOffset,
                            lastEntityState[i].data() + fs.dstOffset,
                            fs.size) != 0) {
              writeField = true;
            }
          }

          if (writeField) {
            AppendToBuffer(tickBuffer, fs.fieldId);
            const uint8_t* src = currentBuf + fs.dstOffset;
            tickBuffer.insert(tickBuffer.end(), src, src + fs.size);

            std::memcpy(lastEntityState[i].data() + fs.dstOffset, src, fs.size);
            fieldsWritten++;
          }
        }

        if (isFullSnapshot) {
          AppendToBuffer(tickBuffer,
                         static_cast<uint16_t>(HDEM_FIELD_CLASSNAME));
          tickBuffer.insert(tickBuffer.end(), slot.className.c_str(),
                            slot.className.c_str() + slot.className.size() + 1);
          fieldsWritten++;

          AppendToBuffer(tickBuffer, static_cast<uint16_t>(HDEM_FIELD_NAME));
          tickBuffer.insert(
              tickBuffer.end(), slot.targetName.c_str(),
              slot.targetName.c_str() + slot.targetName.size() + 1);
          fieldsWritten++;
        }

        tickBuffer[numFieldsOffset] = fieldsWritten;

        lastSeenSerial[i] = slot.serial;
        lastSeenVersion[i] = currentVersion;
        numEntitiesWritten++;
      }
    }
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
