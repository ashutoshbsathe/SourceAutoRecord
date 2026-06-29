#pragma once
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "HdemFormat.hpp"
#include "harness.pb.h"

struct HdemReaderFieldDef {
  uint16_t fieldId;
  std::string name;
  HdemFieldType type;
};

struct HdemReaderClassDef {
  uint16_t classId;
  std::string name;
};

class HdemReader {
 public:
  HdemReader();
  ~HdemReader();

  bool Open(const std::string& path);
  void Close();
  bool IsOpen() const { return file.is_open(); }

  // Apply all frames up to targetTick, rebuilding every entity's state.
  bool AdvanceToTick(int targetTick);

  // Get the current reconstructed snapshot of all entities.
  void GetSnapshot(portal2_harness::EntitySnapshot* outSnapshot, int tick);

  const std::string& GetMapName() const { return mapName; }
  float GetTickrate() const { return tickrate; }

 private:
  bool ReadNextFrame();
  std::string ReadString();

  std::ifstream file;
  std::string mapName;
  float tickrate = 66.666f;
  uint64_t timestamp = 0;
  std::string sarVersion;
  std::string gameDir;
  uint64_t schemaOffset = 0;

  std::unordered_map<uint16_t, HdemReaderClassDef> classes;
  std::unordered_map<uint16_t, HdemReaderFieldDef> fields;

  // Reconstructed state of all entities: entityIndex -> EntityState
  std::unordered_map<int, portal2_harness::EntityState> currentEntities;
  int currentTick = -1;
  bool reachedEof = false;
};
