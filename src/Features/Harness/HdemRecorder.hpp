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

  // Delta tracking: entityIndex -> last written field values (raw bytes)
  std::unordered_map<int, std::vector<uint8_t>> lastEntityState;
  // Track which entity serials existed last tick
  std::unordered_map<int, int> lastEntitySerial;

  // Reused per-tick buffer to eliminate dynamic allocation overhead
  std::vector<uint8_t> tickBuffer;

  size_t totalBytes = 0;
  size_t totalTicks = 0;
};
