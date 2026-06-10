#include <array>
#include <cstdlib>
#include <filesystem>
#include <string>

#include "EntitySnapshotter.hpp"
#include "Features/Demo/Demo.hpp"
#include "Features/Demo/DemoParser.hpp"
#include "Features/Session.hpp"
#include "Features/Tas/TasPlayer.hpp"
#include "Features/Tas/TasScript.hpp"
#include "Harness.hpp"
#include "HarnessThread.hpp"
#include "HdemReader.hpp"
#include "MarkTable.hpp"
#include "Modules/Client.hpp"
#include "Modules/Console.hpp"
#include "Modules/Engine.hpp"
#include "Modules/FileSystem.hpp"
#include "Modules/Server.hpp"
#include "RolloutRecorder.hpp"
#include "SAR.hpp"
#include "Scheduler.hpp"
#include "Utils/Memory.hpp"
#include "Utils/SDK.hpp"

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
  std::string shmName =
      "/portal2_harness_framebuffer_" + std::to_string(instanceN);

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
      "Harness: InitialHandshake called. Responding with: version=%s, map=%s, "
      "shm=%s (%dx%d)\n",
      version.c_str(), map.c_str(), shmName.c_str(), sw, sh);
  response->set_game_version(version);
  response->set_map_name(map);
  response->set_shm_width(shmWidth);
  response->set_shm_height(shmHeight);
  response->set_shm_size(shmSize);
  // Strip leading '/' — POSIX shm_open needs it, but Python SharedMemory does
  // not
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

template <typename T>
static inline T GetFieldValue(const uint8_t* buf, const ClassLayout& layout,
                              uint16_t fieldId) {
  for (const auto& fs : layout.fields) {
    if (fs.fieldId == fieldId) {
      T val;
      std::memcpy(&val, buf + fs.dstOffset, sizeof(T));
      return val;
    }
  }
  return T{};
}

static void PopulateEntityStateProto(
    portal2_harness::EntityState* protoState, int entityIndex,
    const EntitySlot& slot, const ClassLayout& layout,
    const std::vector<HdemFieldDef>& allFields,
    const std::vector<uint8_t>* lastFieldValues) {
  protoState->set_entity_index(entityIndex);
  protoState->set_serial_number(slot.serial);
  protoState->set_class_name(slot.className);
  protoState->set_target_name(slot.targetName);
  // Frame<->telemetry bridge: the same integer drawn on the annotated frame.
  // 0 if this class isn't marked -- and, by design for v0, 0 for ALL entities
  // while sar_harness_annotate is off, since MarkTable is (re)built only by the
  // annotate RENDER handler (HarnessAnnotate.cpp). The eval runs with annotate
  // on, so marks are live; reads here are mutex-guarded (MarkTable.cpp).
  protoState->set_mark(markTable.GetMark(entityIndex, slot.serial));

  const uint8_t* currentBuf = slot.fieldBuf.get();

  Vector position =
      GetFieldValue<Vector>(currentBuf, layout, HDEM_FIELD_ORIGIN);
  protoState->mutable_position()->set_x(position.x);
  protoState->mutable_position()->set_y(position.y);
  protoState->mutable_position()->set_z(position.z);

  QAngle angles = GetFieldValue<QAngle>(currentBuf, layout, HDEM_FIELD_ANGLES);
  protoState->mutable_angles()->set_x(angles.x);
  protoState->mutable_angles()->set_y(angles.y);
  protoState->mutable_angles()->set_z(angles.z);

  Vector velocity =
      GetFieldValue<Vector>(currentBuf, layout, HDEM_FIELD_VELOCITY);
  protoState->mutable_velocity()->set_x(velocity.x);
  protoState->mutable_velocity()->set_y(velocity.y);
  protoState->mutable_velocity()->set_z(velocity.z);

  // Populate dynamic fields
  for (const auto& fs : layout.fields) {
    if (fs.fieldId == HDEM_FIELD_ORIGIN || fs.fieldId == HDEM_FIELD_ANGLES ||
        fs.fieldId == HDEM_FIELD_VELOCITY) {
      continue;
    }

    const uint8_t* bytes = currentBuf + fs.dstOffset;

    if (lastFieldValues && lastFieldValues->size() == slot.fieldBufSize) {
      if (std::memcmp(bytes, lastFieldValues->data() + fs.dstOffset, fs.size) ==
          0) {
        continue;
      }
    }

    auto* fieldProto = protoState->add_fields();
    if (fs.fieldId < allFields.size()) {
      fieldProto->set_name(allFields[fs.fieldId].name);

      switch (allFields[fs.fieldId].type) {
        case HDEM_FLOAT: {
          float val = 0.0f;
          std::memcpy(&val, bytes, sizeof(float));
          fieldProto->set_float_val(val);
          break;
        }
        case HDEM_INT32: {
          int32_t val = 0;
          std::memcpy(&val, bytes, sizeof(int32_t));
          fieldProto->set_int_val(val);
          break;
        }
        case HDEM_VEC3: {
          Vector val;
          std::memcpy(&val, bytes, sizeof(Vector));
          fieldProto->mutable_vec3_val()->set_x(val.x);
          fieldProto->mutable_vec3_val()->set_y(val.y);
          fieldProto->mutable_vec3_val()->set_z(val.z);
          break;
        }
        case HDEM_BOOL: {
          bool val = false;
          std::memcpy(&val, bytes, sizeof(bool));
          fieldProto->set_bool_val(val);
          break;
        }
        case HDEM_STRING: {
          std::string val(reinterpret_cast<const char*>(bytes), fs.size);
          size_t len = std::strlen(val.c_str());
          if (len < val.size()) {
            val.resize(len);
          }
          fieldProto->set_string_val(val);
          break;
        }
        case HDEM_HANDLE: {
          int32_t val = 0;
          std::memcpy(&val, bytes, sizeof(int32_t));
          fieldProto->set_handle_val(val);
          break;
        }
        case HDEM_BYTE: {
          int32_t val = 0;
          val = bytes[0];
          fieldProto->set_int_val(val);
          break;
        }
        case HDEM_SHORT: {
          int16_t val = 0;
          std::memcpy(&val, bytes, sizeof(int16_t));
          fieldProto->set_int_val(val);
          break;
        }
        default:
          break;
      }
    }
  }
}

bool Portal2HarnessImpl::InternalObserve(portal2_harness::GameState* response) {
  if (!session->isRunning) return false;

  Vector position;
  Vector velocity;
  QAngle angles = engine->GetAngles(0);
  int health = 100;
  bool crouching = false;
  int serverTick = 0;

  if (engine->demoplayer->IsPlaying()) {
    serverTick = engine->demoplayer->GetTick();
    response->set_server_tick(serverTick);

    // Use the HDEM sidecar if available
    if (harness && harness->hdemReader && harness->hdemReader->IsOpen()) {
      harness->hdemReader->AdvanceToTick(serverTick);
      harness->hdemReader->GetSnapshot(response->mutable_entity_snapshot(),
                                       serverTick);

      // Find the player entity (index 1) in the HDEM snapshot to extract exact
      // server-side state
      bool foundPlayer = false;
      for (const auto& ent : response->entity_snapshot().entities()) {
        if (ent.entity_index() == 1) {
          response->mutable_position()->CopyFrom(ent.position());
          response->mutable_velocity()->CopyFrom(ent.velocity());

          for (const auto& f : ent.fields()) {
            if (f.name() == "m_iHealth") {
              health = f.int_val();
            } else if (f.name() == "m_bDucked") {
              crouching = f.bool_val();
            }
          }
          response->set_health(health);
          response->set_is_crouching(crouching);
          foundPlayer = true;
          break;
        }
      }

      if (!foundPlayer) {
        response->mutable_position()->set_x(0);
        response->mutable_position()->set_y(0);
        response->mutable_position()->set_z(0);
        response->mutable_velocity()->set_x(0);
        response->mutable_velocity()->set_y(0);
        response->mutable_velocity()->set_z(0);
        response->set_health(100);
        response->set_is_crouching(false);
      }
    } else {
      // Fallback to client-side entity when HDEM is not present
      void* player = client->GetPlayer(1);
      if (!player) return false;
      ClientEnt* pl = (ClientEnt*)player;
      position = pl->abs_origin();
      velocity = pl->abs_velocity();
      crouching = pl->ducked();

      response->mutable_position()->set_x(position.x);
      response->mutable_position()->set_y(position.y);
      response->mutable_position()->set_z(position.z);
      response->mutable_velocity()->set_x(velocity.x);
      response->mutable_velocity()->set_y(velocity.y);
      response->mutable_velocity()->set_z(velocity.z);
      response->set_health(100);
      response->set_is_crouching(crouching);
    }

    response->mutable_camera()->set_x(angles.x);
    response->mutable_camera()->set_y(angles.y);
    response->mutable_camera()->set_z(angles.z);
    return true;
  }

  // Server-side live play branch
  void* player = server->GetPlayer(1);
  if (!player) return false;

  ServerEnt* pl = (ServerEnt*)player;
  position = pl->abs_origin();
  velocity = pl->abs_velocity();
  health = pl->field<int>("m_iHealth");
  crouching = pl->ducked();
  serverTick = server->gpGlobals->tickcount;

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

  if (harness && harness->entitySnapshotter) {
    if (observeLastVersion.size() != (size_t)Offsets::NUM_ENT_ENTRIES) {
      observeLastVersion.assign(Offsets::NUM_ENT_ENTRIES, 0);
      observeLastSerial.assign(Offsets::NUM_ENT_ENTRIES, 0);
      observeLastState.clear();
      observeLastState.resize(Offsets::NUM_ENT_ENTRIES);
    }

    auto* snapshotProto = response->mutable_entity_snapshot();
    snapshotProto->set_tick(serverTick);

    bool sendFull = observeIsFirst || (serverTick < observeLastTick);
    snapshotProto->set_is_full_snapshot(sendFull);

    const auto& allFields = harness->entitySnapshotter->GetFields();

    if (sendFull) {
      for (int i = 0; i < Offsets::NUM_ENT_ENTRIES; ++i) {
        observeLastVersion[i] = 0;
        observeLastSerial[i] = 0;
        observeLastState[i].clear();
      }
      observeIsFirst = false;
    }

    for (int i = 0; i < Offsets::NUM_ENT_ENTRIES; ++i) {
      const auto& slot = harness->entitySnapshotter->GetSlot(i);
      uint16_t oldSerial = observeLastSerial[i];
      bool wasSeen = (oldSerial != 0);
      bool isAlive = slot.alive;

      // 1. Detect and write records for deleted/reused entities
      if (wasSeen && (!isAlive || slot.serial != oldSerial)) {
        auto* protoState = snapshotProto->add_entities();
        protoState->set_entity_index(i);
        protoState->set_serial_number(oldSerial);
        protoState->set_deleted(true);

        observeLastSerial[i] = 0;
        observeLastVersion[i] = 0;
        observeLastState[i].clear();
        wasSeen = false;
      }

      // 2. Detect and write records for new/modified entities
      if (isAlive) {
        uint32_t currentVersion = slot.changeVersion;
        if (!wasSeen || currentVersion != observeLastVersion[i]) {
          bool isNew = !wasSeen;
          const auto& layout =
              harness->entitySnapshotter->GetClassLayout(slot.classId);

          auto* protoState = snapshotProto->add_entities();
          PopulateEntityStateProto(protoState, i, slot, layout, allFields,
                                   isNew ? nullptr : &observeLastState[i]);

          observeLastState[i].assign(slot.fieldBuf.get(),
                                     slot.fieldBuf.get() + slot.fieldBufSize);
          observeLastSerial[i] = slot.serial;
          observeLastVersion[i] = currentVersion;
        }
      }
    }
    observeLastTick = serverTick;
  }

  return true;
}

void Portal2HarnessImpl::ResetObserveState() {
  observeLastVersion.clear();
  observeLastSerial.clear();
  observeLastState.clear();
  observeIsFirst = true;
  observeLastTick = -1;
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

  // Set the framebulk as its own closure -- FIFO ensures it runs before the
  // AdvanceTick burst below, so inputs are in place first. FetchInputs always
  // returns framebulk[0] (the "before" entry for any tick > 0).
  Scheduler::OnMainThread([=]() {
    TasFramebulk& fb = tasPlayer->playbackInfo.slots[0].framebulks[0];
    fb.moveAnalog = {moveX, moveY, 0};
    fb.viewAnalog = {viewX, viewY, 0};
    for (int i = 0; i < TAS_CONTROLLER_INPUT_COUNT; i++) {
      fb.buttonStates[i] = buttons[i];
    }
  });

  AdvanceTicksBlocking(numTicks);

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
    AdvanceTicksBlocking(1);
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
  this->ResetObserveState();

  // Get initial observation
  this->InternalObserve(response->mutable_initial_state());

  // Copy the post-reset (annotated) framebuffer to SHM so the driver's step-0
  // percept has an image -- without this the first frame is stale/blank, since
  // the per-step copy is otherwise only triggered from AgentLoop.
  this->CopyPixelsToShm(context);

  response->set_success(true);
  return grpc::Status::OK;
}

// Copy the current (annotated) framebuffer into SHM on the engine main thread,
// reusing the engine's ReadScreenPixels path. Returns false only if the client
// cancelled the stream mid-read; a missing/zero SHM or videomode is a no-op
// (returns true -- not an error).
bool Portal2HarnessImpl::CopyPixelsToShm(grpc::ServerContext* context) {
  if (shm.GetBuffer() == MAP_FAILED || shm.GetSize() == 0 ||
      !g_harness_videomode || !*g_harness_videomode) {
    return true;
  }
  return RunOnMainThreadSync(context, [&]() {
    int sw = 854;
    int sh = 480;
    if (engine && engine->GetScreenSize) {
      engine->GetScreenSize(nullptr, sw, sh);
    }
    Memory::VMT<void(__rescall*)(void*, int, int, int, int, void*, int)>(
        *g_harness_videomode, Offsets::ReadScreenPixels)(
        *g_harness_videomode, 0, 0, sw, sh, shm.GetBuffer(),
        2 /* IMAGE_FORMAT_RGB888 */);
  });
}

// PR1 stub: the real macro executor (MacroExecutor) lands in PR2. Until then
// every verb reports NOT_IMPLEMENTED so the wire format + dispatch can be
// exercised end-to-end.
void Portal2HarnessImpl::ExecuteMacro(
    const portal2_harness::MacroRequest* request,
    portal2_harness::MacroResult* result) {
  result->set_ok(false);
  result->set_result_code("NOT_IMPLEMENTED");
  result->set_detail("verb '" + request->verb() +
                     "' not implemented (PR1 stub)");
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

  // Force a full entity snapshot on the first observation of this stream.
  this->ResetObserveState();

  portal2_harness::AgentMessage req;
  while (stream->Read(&req)) {
    portal2_harness::EnvironmentMessage env_msg;
    env_msg.set_success(true);

    if (req.has_macro()) {
      // Macro path: run one closed semantic verb. The refreshed percept still
      // rides on the Observe below, shared with the action path.
      this->ExecuteMacro(&req.macro(), env_msg.mutable_macro_result());
    } else {
      // Raw framebulk path (existing RL behavior).
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

    // Refresh the visual percept when requested (the model sets this once per
    // step). Shared by the action and macro branches.
    if (req.copy_pixels_to_shm()) {
      if (!this->CopyPixelsToShm(context)) return grpc::Status::CANCELLED;
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
  if (outputPath.empty())
    outputPath = demoPath + ".rollout";
  else if (!Utils::EndsWith(outputPath, ".rollout"))
    outputPath += ".rollout";

  // Ensure absolute path in the game directory if it is relative
  bool isAbsolute = (outputPath.size() >= 1 && outputPath[0] == '/');
#ifdef _WIN32
  if (outputPath.size() >= 2 && outputPath[1] == ':') isAbsolute = true;
#endif
  if (!isAbsolute) {
    outputPath = std::string(engine->GetGameDirectory()) + "/" + outputPath;
  }

  // Check if demo file exists
  auto fullPath = fileSystem->FindFileSomewhere(demoPath).value_or(demoPath);
  if (!std::filesystem::exists(fullPath)) {
    response->set_success(false);
    response->set_error_message("Demo file not found: " + demoPath);
    return grpc::Status::OK;
  }

  console->Print("Harness: RenderDemo initiating playback for %s\n",
                 demoPath.c_str());

  std::string shmName = std::string("portal2_harness_framebuffer_") +
                        harness->instanceId.GetString();

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

    if (harness->hdemReader) {
      delete harness->hdemReader;
      harness->hdemReader = nullptr;
    }

    std::string hdemPath = fullPath.substr(0, fullPath.size() - 4) + ".hdem";
    if (std::filesystem::exists(hdemPath)) {
      harness->hdemReader = new HdemReader();
      if (!harness->hdemReader->Open(hdemPath)) {
        console->Warning("Harness: Failed to open sidecar .hdem file: %s\n",
                         hdemPath.c_str());
        delete harness->hdemReader;
        harness->hdemReader = nullptr;
      } else {
        console->Print(
            "Harness: Opened sidecar .hdem file for rollout overlay: %s\n",
            hdemPath.c_str());
      }
    }

    int sw = 854;
    int sh = 480;
    if (engine && engine->GetScreenSize) {
      engine->GetScreenSize(nullptr, sw, sh);
    }

    if (!harness->rolloutRecorder->Start(outputPath, targetMapName, shmName, sw,
                                         sh, 1.0f / engine->GetIPT(),
                                         capturePixels)) {
      if (harness->hdemReader) {
        delete harness->hdemReader;
        harness->hdemReader = nullptr;
      }
      console->Warning("Harness: Failed to open rollout file for writing!\n");
      setupDone.store(true);
      return;
    }

    {
      std::lock_guard<std::mutex> lock(harness->recordingMutex);
      harness->isRecordingRollout = true;
      harness->wasPlayingDemo = false;
    }

    sv_alternateticks.SetValue(0);

    std::string cmd =
        "sar_disable_challenge_stats_hud -1; hideconsole; playdemo \"" +
        demoPath + "\"";
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

  // Sleep cleanly on the server-side condition variable until recording
  // completes end-to-end
  std::unique_lock<std::mutex> lock(harness->recordingMutex);
  while (harness->isRecordingRollout.load()) {
    if (context->IsCancelled()) break;
    harness->recordingCV.wait_for(lock, std::chrono::milliseconds(50));
  }

  if (context->IsCancelled()) {
    // If canceled by client timeout, dispatch Stop to ensure file stream
    // flushes safely
    Scheduler::OnMainThread([]() {
      if (harness && harness->isRecordingRollout) {
        harness->rolloutRecorder->Stop();
        {
          std::lock_guard<std::mutex> lk(harness->recordingMutex);
          harness->isRecordingRollout = false;
          harness->wasPlayingDemo = false;
          harness->recordingCV.notify_all();
        }
        sv_alternateticks.SetValue(1);
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
