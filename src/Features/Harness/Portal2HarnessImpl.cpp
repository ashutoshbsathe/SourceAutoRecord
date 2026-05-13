#include <array>
#include <cstdlib>
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
#include <filesystem>
#include "Modules/FileSystem.hpp"
#include "RolloutRecorder.hpp"
#include "Features/Demo/Demo.hpp"
#include "Features/Demo/DemoParser.hpp"

extern void** g_harness_videomode_ptr;
#define g_harness_videomode g_harness_videomode_ptr

Portal2HarnessImpl::Portal2HarnessImpl() {}

grpc::Status Portal2HarnessImpl::InitialHandshake(
    grpc::ServerContext* context,
    const portal2_harness::HandshakeRequest* request,
    portal2_harness::HandshakeResponse* response) {
  std::string version = sar.game->Version();
  std::string map = engine->GetCurrentMapName();

  // Derive SHM name from instance ID (sar_harness_instance N)
  int instanceN = harness ? harness->GetInstanceId() : 0;
  std::string shmName = "/portal2_harness_framebuffer_" + std::to_string(instanceN);

  int sw = 854;
  int sh = 480;
  if (engine && engine->GetScreenSize) {
    engine->GetScreenSize(nullptr, sw, sh);
  }

  size_t shmWidth = sw;
  size_t shmHeight = sh;
  size_t shmSize = shmWidth * shmHeight * 3;
  shm.Init(shmName, shmSize);

  console->Print(
      "Harness: InitialHandshake called. Responding with: version=%s, map=%s, shm=%s (%dx%d)\n",
      version.c_str(), map.c_str(), shmName.c_str(), sw, sh);
  response->set_game_version(version);
  response->set_map_name(map);
  response->set_shm_width(shmWidth);
  response->set_shm_height(shmHeight);
  response->set_shm_size(shmSize);
  // Strip leading '/' — POSIX shm_open needs it, but Python SharedMemory does not
  response->set_shm_name(shmName.substr(1));
  return grpc::Status::OK;
}

grpc::Status Portal2HarnessImpl::Observe(grpc::ServerContext* context,
                                         const portal2_harness::Empty* request,
                                         portal2_harness::GameState* response) {
  if (this->InternalObserve(response)) {
    return grpc::Status::OK;
  }
  return grpc::Status(grpc::StatusCode::INTERNAL, "Failed to observe state");
}

bool Portal2HarnessImpl::InternalObserve(portal2_harness::GameState* response) {
  if (!session->isRunning) return false;

  void* player = nullptr;
  bool isDemo = engine->demoplayer->IsPlaying();

  if (isDemo) {
    player = client->GetPlayer(1);
  } else {
    player = server->GetPlayer(1);
  }

  if (!player) return false;

  Vector position;
  Vector velocity;
  QAngle angles = engine->GetAngles(0);
  int health = 100;
  bool crouching = false;
  int serverTick = 0;

  if (isDemo) {
    ClientEnt* cl = (ClientEnt*)player;
    position = cl->abs_origin();
    velocity = cl->abs_velocity();
    crouching = cl->ducked();
    serverTick = engine->GetTick();
    // Health isn't easily available on client without more work, 100 is fine for demos
  } else {
    ServerEnt* pl = (ServerEnt*)player;
    position = pl->abs_origin();
    velocity = pl->abs_velocity();
    health = pl->field<int>("m_iHealth");
    crouching = pl->ducked();
    serverTick = server->gpGlobals->tickcount;
  }

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

  return true;
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

// docs/Portal2HarnessImpl.cpp:AgentLoop>
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
          int sw = 854;
          int sh = 480;
          if (engine && engine->GetScreenSize) {
            engine->GetScreenSize(nullptr, sw, sh);
          }
          Memory::VMT<void(__rescall*)(void*, int, int, int, int, void*, int)>(
              *g_harness_videomode, Offsets::ReadScreenPixels)(
              *g_harness_videomode, 0, 0, sw, sh, shm.GetBuffer(),
              2 /* IMAGE_FORMAT_RGB888 */);
          pixelsRead.store(true);
        });

        while (!pixelsRead.load()) {
          if (context->IsCancelled()) return grpc::Status::CANCELLED;
          std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
      }
    }

    stream->Write(env_msg);
  }

  return grpc::Status::OK;
}

grpc::Status Portal2HarnessImpl::RenderDemo(
    grpc::ServerContext* context,
    const portal2_harness::RenderDemoRequest* request,
    portal2_harness::RenderDemoResponse* response) {
  std::string demoPath = request->demo_path();
  std::string outputPath = request->output_path();
  bool capturePixels = request->capture_pixels();

  if (demoPath.empty()) {
    response->set_success(false);
    response->set_error_message("Empty demo_path");
    return grpc::Status::OK;
  }

  if (!Utils::EndsWith(demoPath, ".dem")) demoPath += ".dem";
  if (outputPath.empty()) outputPath = demoPath + ".rollout";
  else if (!Utils::EndsWith(outputPath, ".rollout")) outputPath += ".rollout";

  // Ensure absolute path in the game directory if it's just a filename
  if (outputPath.find('/') == std::string::npos && outputPath.find('\\') == std::string::npos) {
    outputPath = std::string(engine->GetGameDirectory()) + "/" + outputPath;
  }

  // Check if demo file exists
  auto fullPath = fileSystem->FindFileSomewhere(demoPath).value_or(demoPath);
  if (!std::filesystem::exists(fullPath)) {
    response->set_success(false);
    response->set_error_message("Demo file not found: " + demoPath);
    return grpc::Status::OK;
  }

  console->Print("Harness: RenderDemo initiating playback for %s\n", demoPath.c_str());

  std::string shmName = std::string("portal2_harness_framebuffer_") + harness->instanceId.GetString();

  std::string targetMapName = engine->GetCurrentMapName();
  {
    DemoParser parser;
    parser.headerOnly = true;
    Demo demoHeader;
    if (parser.Parse(fullPath, &demoHeader)) {
      targetMapName = demoHeader.mapName;
    }
  }

  // Dispatch playdemo execution and recorder start to main thread
  std::atomic<bool> setupDone{false};
  std::atomic<bool> setupSuccess{false};

  Scheduler::OnMainThread([&]() {
    // Unpause the engine so demo playback frames actually advance freely
    engine->SetAdvancing(false);
    harness->harnessControlActive = false;
    harness->warmupTicksRemaining = 0;
    harness->ticksRemaining = 0;

    // Wake up any lingering Act() threads if needed
    {
      std::lock_guard<std::mutex> lk(harness->tickMutex);
      harness->tickCV.notify_all();
    }

    int sw = 854;
    int sh = 480;
    if (engine && engine->GetScreenSize) {
      engine->GetScreenSize(nullptr, sw, sh);
    }

    if (!harness->rolloutRecorder->Start(outputPath, targetMapName,
                                         shmName, sw, sh, 1.0f / engine->GetIPT(),
                                         capturePixels)) {
      console->Warning("Harness: Failed to open rollout file for writing!\n");
      setupDone.store(true);
      return;
    }

    {
      std::lock_guard<std::mutex> lock(harness->recordingMutex);
      harness->isRecordingRollout = true;
      harness->wasPlayingDemo = false;
    }

    std::string cmd = "sar_disable_challenge_stats_hud -1; hideconsole; playdemo \"" + demoPath + "\"";
    engine->ExecuteCommand(cmd.c_str(), false);
    setupSuccess.store(true);
    setupDone.store(true);
  });

  while (!setupDone.load()) {
    if (context->IsCancelled()) return grpc::Status::CANCELLED;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  if (!setupSuccess.load()) {
    response->set_success(false);
    response->set_error_message("Failed to initialize rollout file writer");
    return grpc::Status::OK;
  }

  // Sleep cleanly on the server-side condition variable until recording completes end-to-end
  std::unique_lock<std::mutex> lock(harness->recordingMutex);
  while (harness->isRecordingRollout.load()) {
    if (context->IsCancelled()) break;
    harness->recordingCV.wait_for(lock, std::chrono::milliseconds(50));
  }

  if (context->IsCancelled()) {
    // If canceled by client timeout, dispatch Stop to ensure file stream flushes safely
    Scheduler::OnMainThread([]() {
      if (harness && harness->isRecordingRollout) {
        harness->rolloutRecorder->Stop();
        {
          std::lock_guard<std::mutex> lk(harness->recordingMutex);
          harness->isRecordingRollout = false;
          harness->wasPlayingDemo = false;
          harness->recordingCV.notify_all();
        }
      }
    });
    return grpc::Status::CANCELLED;
  }

  response->set_success(true);
  response->set_final_output_path(outputPath);
  response->set_recorded_ticks(harness->rolloutRecorder->recordedTicks);
  response->set_total_bytes(harness->rolloutRecorder->totalBytes);
  return grpc::Status::OK;
}
