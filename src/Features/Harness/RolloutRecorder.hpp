#pragma once
#include <fstream>
#include <string>

#include "Utils/SDK.hpp"
#include "Utils/SDK/UserCmd.hpp"
#include "harness.pb.h"

class RolloutRecorder {
 public:
  RolloutRecorder();
  ~RolloutRecorder();

  bool Start(const std::string& path, const std::string& mapName,
             const std::string& shmName, int width, int height, float tickrate,
             bool capturePixels);
  void Stop();
  bool IsActive() const { return isActive; }
  bool CapturesPixels() const { return capturePixels; }

  void RecordTick(const portal2_harness::GameState& state,
                  const portal2_harness::ActionRequest& action, void* pixels,
                  size_t pixelSize);

  void* GetBuffer() { return pixelBuffer; }
  size_t GetBufferSize() { return pixelBufferSize; }

  // TODO: mouse movement could be noisy since we reconstruct it from viewangles
  // instead of directly reading it from CUserCmd
  void MapUserCmdToAction(const CUserCmd& cmd,
                          portal2_harness::ActionRequest* req);

 private:
  void WriteMessage(const google::protobuf::Message& msg);

  std::ofstream file;
  bool isActive = false;
  bool capturePixels = false;
  void* pixelBuffer = nullptr;
  size_t pixelBufferSize = 0;

  bool hasLastAngles = false;
  QAngle lastAngles;

 public:
  size_t recordedTicks = 0;
  size_t totalBytes = 0;
};
