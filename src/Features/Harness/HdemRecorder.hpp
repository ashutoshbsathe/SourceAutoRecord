#pragma once
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "HdemFormat.hpp"
#include "Utils/SDK.hpp"

class HdemRecorder {
 public:
  HdemRecorder() = default;
  ~HdemRecorder();

  bool Start(const std::string& path, const std::string& mapName,
             float tickrate);
  void Stop();
  bool IsActive() const { return isActive; }
  void RecordTick(int tickNumber);

 private:
  void WriteHeader(const std::string& mapName, float tickrate);
  void WriteClassTable();
  void WriteFieldTable();

  std::ofstream file;
  std::streampos schemaOffsetPos;
  bool isActive = false;
  bool headerWritten = false;

  // Delta tracking arrays, sized dynamically to Offsets::NUM_ENT_ENTRIES
  std::vector<uint32_t> lastSeenVersion;
  std::vector<uint16_t> lastSeenSerial;
  std::vector<std::vector<uint8_t>> lastEntityState;

  // Reused per-tick buffer to eliminate dynamic allocation overhead
  std::vector<uint8_t> tickBuffer;

  size_t totalBytes = 0;
  size_t totalTicks = 0;
  int lastRecordedTick = -1;
};
