#include <array>
#include <string>

#include "Features/Session.hpp"
#include "Features/Tas/TasPlayer.hpp"
#include "Features/Tas/TasScript.hpp"
#include "Harness.hpp"
#include "Modules/Console.hpp"
#include "Modules/Engine.hpp"
#include "Modules/Server.hpp"
#include "SAR.hpp"
#include "Scheduler.hpp"
#include "Utils/Memory.hpp"
#include "Utils/SDK.hpp"

static void** g_harness_videomode = nullptr;

void Portal2Harness_InitVideoMode(void** videomode) {
  g_harness_videomode = videomode;
}

Portal2HarnessImpl::Portal2HarnessImpl() {}

grpc::Status Portal2HarnessImpl::InitialHandshake(
    grpc::ServerContext* context,
    const portal2_harness::HandshakeRequest* request,
    portal2_harness::HandshakeResponse* response) {
  std::string version = sar.game->Version();
  std::string map = engine->GetCurrentMapName();

  // Initialize SHM for the POC
  size_t shmWidth = 854;
  size_t shmHeight = 480;
  size_t shmSize = shmWidth * shmHeight * 3;
  shm.Init("/portal2_harness_framebuffer", shmSize);

  console->Print(
      "Harness: InitialHandshake called. Responding with: version=%s, map=%s\n",
      version.c_str(), map.c_str());
  response->set_game_version(version);
  response->set_map_name(map);
  response->set_shm_width(shmWidth);
  response->set_shm_height(shmHeight);
  response->set_shm_size(shmSize);
  return grpc::Status::OK;
}

grpc::Status Portal2HarnessImpl::Observe(grpc::ServerContext* context,
                                         const portal2_harness::Empty* request,
                                         portal2_harness::GameState* response) {
  if (!session->isRunning) {
    return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION,
                        "No session running");
  }

  // Get player entity (slot 0, index 1)
  void* player = server->GetPlayer(1);
  if (!player) {
    return grpc::Status(grpc::StatusCode::INTERNAL,
                        "Failed to get player entity");
  }

  ServerEnt* pl = (ServerEnt*)player;

  // Read player state
  Vector position = pl->abs_origin();
  Vector velocity = pl->abs_velocity();
  QAngle angles = engine->GetAngles(0);
  int health = pl->field<int>("m_iHealth");
  bool crouching = pl->ducked();
  int serverTick = server->gpGlobals->tickcount;

  // Fill response
  response->mutable_position()->set_x(position.x);
  response->mutable_position()->set_y(position.y);
  response->mutable_position()->set_z(position.z);

  response->mutable_velocity()->set_x(velocity.x);
  response->mutable_velocity()->set_y(velocity.y);
  response->mutable_velocity()->set_z(velocity.z);

  response->mutable_camera()->set_x(angles.x);
  response->mutable_camera()->set_y(angles.y);
  response->mutable_camera()->set_z(angles.z);

  response->set_health(health);
  response->set_is_crouching(crouching);
  response->set_server_tick(serverTick);

  return grpc::Status::OK;
}

grpc::Status Portal2HarnessImpl::Act(
    grpc::ServerContext* context, const portal2_harness::ActionRequest* request,
    portal2_harness::ActionResponse* response) {
  if (!session->isRunning) {
    response->set_success(false);
    response->set_error_message("No session running");
    return grpc::Status::OK;
  }

  if (!harness->harnessControlActive) {
    response->set_success(false);
    response->set_error_message(
        "Harness control not active (warmup may not be complete)");
    return grpc::Status::OK;
  }

  if (!tasPlayer || !tasPlayer->IsActive()) {
    response->set_success(false);
    response->set_error_message("TasPlayer not active");
    return grpc::Status::OK;
  }

  int numTicks = request->num_ticks();
  if (numTicks <= 0) numTicks = 1;

  // Build the framebulk from the ActionRequest
  float moveX = 0.0f;
  float moveY = 0.0f;
  if (request->key_forward()) moveY += 1.0f;
  if (request->key_backward()) moveY -= 1.0f;
  if (request->key_right()) moveX += 1.0f;
  if (request->key_left()) moveX -= 1.0f;

  float viewX = request->mouse_dx();
  float viewY = request->mouse_dy();

  std::array<bool, TAS_CONTROLLER_INPUT_COUNT> buttons = {false};
  if (request->key_jump()) buttons[Jump] = true;
  if (request->key_crouch()) buttons[Crouch] = true;
  if (request->key_use()) buttons[Use] = true;
  if (request->key_zoomin()) buttons[Zoom] = true;
  if (request->key_zoomout()) buttons[Zoom] = true;
  if (request->portal_primary()) buttons[FireBlue] = true;
  if (request->portal_secondary()) buttons[FireOrange] = true;

  // Set the number of ticks we want to advance
  harness->ticksRemaining = numTicks;

  // Dispatch framebulk update and tick advancing to the main thread
  Scheduler::OnMainThread([=]() {
    // Update the first framebulk (index 0) with our harness inputs.
    // FetchInputs binary-searches framebulks and always returns this one
    // (it's the "before" entry for any tick > 0).
    TasFramebulk& fb = tasPlayer->playbackInfo.slots[0].framebulks[0];
    fb.moveAnalog = {moveX, moveY, 0};
    fb.viewAnalog = {viewX, viewY, 0};
    for (int i = 0; i < TAS_CONTROLLER_INPUT_COUNT; i++) {
      fb.buttonStates[i] = buttons[i];
    }

    // Advance the requested number of ticks
    for (int i = 0; i < numTicks; i++) {
      engine->AdvanceTick();
    }
  });

  // Wait for all ticks to execute (signaled from PRE_TICK handler)
  {
    std::unique_lock<std::mutex> lock(harness->tickMutex);
    harness->tickCV.wait(lock, []() { return harness->ticksRemaining <= 0; });
  }

  response->set_success(true);
  return grpc::Status::OK;
}

grpc::Status Portal2HarnessImpl::ExecuteCommand(
    grpc::ServerContext* context,
    const portal2_harness::CommandRequest* request,
    portal2_harness::CommandResponse* response) {
  std::string cmd = request->command();
  if (cmd.empty()) {
    response->set_success(false);
    response->set_error_message("Empty command");
    return grpc::Status::OK;
  }

  console->Print("Harness: ExecuteCommand: %s\n", cmd.c_str());

  // Dispatch command to main thread
  Scheduler::OnMainThread(
      [cmd]() { engine->ExecuteCommand(cmd.c_str(), true); });

  // Advance a tick so the command takes effect
  if (harness->harnessControlActive) {
    harness->ticksRemaining = 1;
    Scheduler::OnMainThread([]() { engine->AdvanceTick(); });

    std::unique_lock<std::mutex> lock(harness->tickMutex);
    harness->tickCV.wait(lock, []() { return harness->ticksRemaining <= 0; });
  }

  response->set_success(true);
  return grpc::Status::OK;
}

grpc::Status Portal2HarnessImpl::Reset(
    grpc::ServerContext* context, const portal2_harness::ResetRequest* request,
    portal2_harness::ResetResponse* response) {
  if (!harness || !harness->IsEnabled()) {
    response->set_success(false);
    response->set_error_message("Harness not enabled");
    return grpc::Status::OK;
  }

  std::string mapName = request->map_name();
  console->Print("Harness: Reset called (map=%s)\n",
                 mapName.empty() ? "<current>" : mapName.c_str());

  // Deactivate harness control so the game can run freely during load
  harness->harnessControlActive = false;
  harness->warmupTicksRemaining = 0;
  harness->ticksRemaining = 0;

  // Wake up any in-flight Act() call (e.g. from a cancelled AgentLoop stream)
  // so it can return and free tickMutex for the next episode.
  {
    std::lock_guard<std::mutex> lock(harness->tickMutex);
    harness->tickCV.notify_all();
  }

  // Dispatch the restart to the main thread
  Scheduler::OnMainThread([mapName]() {
    // Unpause the game so it can actually load
    engine->SetAdvancing(false);

    // Stop TasPlayer (will be re-activated by SESSION_START handler)
    if (tasPlayer && tasPlayer->IsActive()) {
      tasPlayer->Stop(true);
    }

    if (mapName.empty()) {
      engine->ExecuteCommand("restart_level", true);
    } else {
      std::string cmd = "map " + mapName;
      engine->ExecuteCommand(cmd.c_str(), true);
    }
  });

  // Wait for the warmup to complete (signaled from PRE_TICK when warmup hits 0)
  {
    std::unique_lock<std::mutex> lock(harness->resetMutex);
    harness->resetCV.wait(
        lock, []() { return harness->harnessControlActive.load(); });
  }

  console->Print("Harness: Reset complete, harness control re-established\n");

  // Get initial observation
  portal2_harness::GameState* state = response->mutable_initial_state();

  void* player = server->GetPlayer(1);
  if (player) {
    ServerEnt* pl = (ServerEnt*)player;
    Vector position = pl->abs_origin();
    Vector velocity = pl->abs_velocity();
    QAngle angles = engine->GetAngles(0);

    state->mutable_position()->set_x(position.x);
    state->mutable_position()->set_y(position.y);
    state->mutable_position()->set_z(position.z);
    state->mutable_velocity()->set_x(velocity.x);
    state->mutable_velocity()->set_y(velocity.y);
    state->mutable_velocity()->set_z(velocity.z);
    state->mutable_camera()->set_x(angles.x);
    state->mutable_camera()->set_y(angles.y);
    state->mutable_camera()->set_z(angles.z);
    state->set_health(pl->field<int>("m_iHealth"));
    state->set_is_crouching(pl->ducked());
    state->set_server_tick(server->gpGlobals->tickcount);
  }

  response->set_success(true);
  return grpc::Status::OK;
}

//docs/Portal2HarnessImpl.cpp:AgentLoop>
grpc::Status Portal2HarnessImpl::AgentLoop(
    grpc::ServerContext* context,
    grpc::ServerReaderWriter<portal2_harness::EnvironmentMessage,
                             portal2_harness::AgentMessage>* stream) {
  if (!session->isRunning) {
    return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION,
                        "No session running");
  }

  portal2_harness::AgentMessage req;
  while (stream->Read(&req)) {
    portal2_harness::EnvironmentMessage env_msg;
    env_msg.set_success(true);

    portal2_harness::ActionResponse action_resp;
    grpc::Status act_status = this->Act(context, &req.action(), &action_resp);

    if (!act_status.ok()) {
      env_msg.set_success(false);
      env_msg.set_error_message("Act failed: " + act_status.error_message());
      stream->Write(env_msg);
      continue;
    } else if (!action_resp.success()) {
      env_msg.set_success(false);
      env_msg.set_error_message(action_resp.error_message());
      stream->Write(env_msg);
      continue;
    }

    portal2_harness::Empty empty_req;
    grpc::Status obs_status =
        this->Observe(context, &empty_req, env_msg.mutable_state());

    if (!obs_status.ok()) {
      env_msg.set_success(false);
      env_msg.set_error_message("Observe failed: " +
                                obs_status.error_message());
      stream->Write(env_msg);
      continue;
    }

    if (req.copy_pixels_to_shm()) {
      if (shm.GetBuffer() != MAP_FAILED && shm.GetSize() > 0 &&
          g_harness_videomode && *g_harness_videomode) {
        std::atomic<bool> pixelsRead{false};
        Scheduler::OnMainThread([&]() {
          Memory::VMT<void(__rescall*)(void*, int, int, int, int, void*, int)>(
              *g_harness_videomode, Offsets::ReadScreenPixels)(
              *g_harness_videomode, 0, 0, 854, 480, shm.GetBuffer(),
              2 /* IMAGE_FORMAT_RGB888 */);
          pixelsRead.store(true);
        });

        while (!pixelsRead.load()) {
          std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
      }
    }

    stream->Write(env_msg);
  }

  return grpc::Status::OK;
}
