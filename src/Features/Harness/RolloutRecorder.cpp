#include "RolloutRecorder.hpp"

#include <cstdint>

#include "Utils/SDK/GameMovement.hpp"

RolloutRecorder::RolloutRecorder() : isActive(false), capturePixels(false) {}

RolloutRecorder::~RolloutRecorder() {
  this->Stop();
  if (this->pixelBuffer) {
    free(this->pixelBuffer);
  }
}

bool RolloutRecorder::Start(const std::string& path, const std::string& mapName,
                            const std::string& shmName, int width, int height,
                            float tickrate, bool capturePixels) {
  if (this->isActive) this->Stop();

  this->file.open(path, std::ios::binary | std::ios::out);
  if (!this->file.is_open()) return false;

  this->isActive = true;
  this->capturePixels = capturePixels;
  this->recordedTicks = 0;
  this->totalBytes = 0;

  if (this->capturePixels) {
    size_t needed = width * height * 3;
    if (needed > this->pixelBufferSize) {
      this->pixelBuffer = realloc(this->pixelBuffer, needed);
      this->pixelBufferSize = needed;
    }
  }

  // Write Header
  portal2_harness::RolloutHeader header;
  header.set_map_name(mapName);
  header.set_shm_width(width);
  header.set_shm_height(height);
  header.set_tickrate(tickrate);
  header.set_shm_name(shmName);
  this->WriteMessage(header);

  return true;
}

void RolloutRecorder::Stop() {
  if (!this->isActive) return;

  this->file.close();
  this->isActive = false;

  // We can't use console->Print here because we are a lower level util,
  // so we'll just log to a string if needed or let the caller handle it.
}

void RolloutRecorder::RecordTick(const portal2_harness::GameState& state,
                                 const portal2_harness::ActionRequest& action,
                                 void* pixels, size_t pixelSize) {
  if (!this->isActive) return;

  portal2_harness::RolloutStep step;
  step.mutable_state()->CopyFrom(state);
  step.mutable_action()->CopyFrom(action);

  if (this->capturePixels && pixels && pixelSize > 0) {
    step.set_image_data(pixels, pixelSize);
  }

  this->WriteMessage(step);
  this->recordedTicks++;
}

void RolloutRecorder::WriteMessage(const google::protobuf::Message& msg) {
  std::string serialized;
  msg.SerializeToString(&serialized);
  uint32_t size = serialized.size();
  this->file.write(reinterpret_cast<const char*>(&size), sizeof(size));
  this->file.write(serialized.data(), size);
  this->totalBytes += (sizeof(size) + size);
}

void RolloutRecorder::MapUserCmdToAction(const CUserCmd& cmd,
                                         portal2_harness::ActionRequest* req) {
  req->set_num_ticks(1);
  req->set_key_forward(cmd.buttons & IN_FORWARD);
  req->set_key_backward(cmd.buttons & IN_BACK);
  req->set_key_left(cmd.buttons & IN_MOVELEFT);
  req->set_key_right(cmd.buttons & IN_MOVERIGHT);
  req->set_key_jump(cmd.buttons & IN_JUMP);
  req->set_key_crouch(cmd.buttons & IN_DUCK);
  req->set_key_use(cmd.buttons & IN_USE);
  req->set_key_zoomin(cmd.buttons & IN_ZOOM);
  req->set_key_zoomout(cmd.buttons & IN_ZOOM);
  req->set_portal_primary(cmd.buttons & IN_ATTACK);
  req->set_portal_secondary(cmd.buttons & IN_ATTACK2);

  // Scale mousedx/y. Source raw mouse delta is roughly in pixels.
  // For ActionRequest, we usually expect a [-1, 1] range representing a
  // "normalized" move, but many agents use raw degrees. We'll pass them as is
  // for now.
  req->set_mouse_dx(static_cast<float>(cmd.mousedx));
  req->set_mouse_dy(static_cast<float>(cmd.mousedy));
}
