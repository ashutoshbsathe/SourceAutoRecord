#include "Harness.hpp"

#include <array>
#include <climits>
#include <string>

#include "EntitySnapshotter.hpp"
#include "Event.hpp"
#include "Features/Demo/Demo.hpp"
#include "Features/Demo/DemoParser.hpp"
#include "Features/Session.hpp"
#include "Features/Tas/TasController.hpp"
#include "Features/Tas/TasPlayer.hpp"
#include "Features/Tas/TasScript.hpp"
#include "HdemRecorder.hpp"
#include "Modules/Client.hpp"
#include "Modules/Console.hpp"
#include "Modules/Engine.hpp"
#include "Modules/FileSystem.hpp"
#include "Modules/Server.hpp"
#include "RolloutRecorder.hpp"
#include "SAR.hpp"
#include "Scheduler.hpp"
#include "Utils/SDK.hpp"

void** g_harness_videomode_ptr = nullptr;

void Portal2Harness_InitVideoMode(void** videomode) {
  g_harness_videomode_ptr = videomode;
}

Harness* harness;

const int HARNESS_WARMUP_TICKS = 256;

// ================================================================
// Helpers
// ================================================================

// Build a TasPlaybackInfo suitable for harness control.
// Contains two framebulks: a default at tick 0 (the one we update),
// and a sentinel at a very high tick (to keep lastTick huge so
// TasPlayer::Update never auto-stops).
static TasPlaybackInfo BuildHarnessPlaybackInfo() {
  TasPlaybackInfo info;

  // Configure header: start immediately, no map change
  info.slots[0].header.version = 5;  // latest script version
  info.slots[0].header.startInfo.isNext = false;
  info.slots[0].header.startInfo.type = StartImmediately;
  info.slots[0].header.startInfo.param = "";
  info.slots[0].header.rngManipFile = "";

  // Mark as raw playback (skip TAS tool processing)
  info.slots[0].forceRawPlayback = true;
  info.slots[0].loadedFromFile = false;
  info.slots[0].name = "harness";

  // Default framebulk at tick 0 (do nothing)
  TasFramebulk fb0;
  fb0.tick = 0;
  fb0.moveAnalog = {0, 0, 0};
  fb0.viewAnalog = {0, 0, 0};
  for (int i = 0; i < TAS_CONTROLLER_INPUT_COUNT; i++) {
    fb0.buttonStates[i] = false;
  }
  info.slots[0].framebulks.push_back(fb0);

  // Sentinel framebulk at a very high tick to prevent auto-stop
  TasFramebulk fbSentinel;
  fbSentinel.tick = INT_MAX / 2;
  fbSentinel.moveAnalog = {0, 0, 0};
  fbSentinel.viewAnalog = {0, 0, 0};
  for (int i = 0; i < TAS_CONTROLLER_INPUT_COUNT; i++) {
    fbSentinel.buttonStates[i] = false;
  }
  info.slots[0].framebulks.push_back(fbSentinel);

  // Slot 1 unused (single player)
  info.coopControlSlot = -1;

  return info;
}

// ================================================================
// Harness implementation
// ================================================================

static void ActivateHarnessTasPlayer() {
  if (!tasPlayer || !tasControllers[0]) {
    console->Warning("Harness: TasPlayer or TasController not initialized!\n");
    return;
  }

  console->Print("Harness: Activating TasPlayer with harness framebulks...\n");
  TasPlaybackInfo info = BuildHarnessPlaybackInfo();
  tasPlayer->Activate(info);
  // TasPlayer::Update() will call Start() and PostStart() on subsequent frames,
  // which handles in_forceuser, controller enabling, etc.
}

static void sar_harness_callback(void* var, const char* pOldValue,
                                 float flOldValue) {
  if (harness->IsEnabled()) {
    console->Print("Harness enabled. Initializing gRPC server thread...\n");
    harness->StartServer();

    // If a session is already running (user enabled harness mid-game),
    // activate TasPlayer and start warmup
    if (session->isRunning) {
      ActivateHarnessTasPlayer();
      harness->warmupTicksRemaining = HARNESS_WARMUP_TICKS;
      harness->harnessControlActive = false;
    }
  } else {
    console->Print("Harness disabled. Shutting down gRPC server...\n");
    harness->harnessControlActive = false;
    harness->warmupTicksRemaining = 0;
    harness->ticksRemaining = 0;
    // Stop TasPlayer if it's running
    if (tasPlayer && tasPlayer->IsActive()) {
      Scheduler::OnMainThread([]() { tasPlayer->Stop(true); });
    }
    harness->StopServer();
  }
}

Harness::Harness()
    : enabled("sar_harness", "0", "Enables the Harness feature.\n", 0,
              sar_harness_callback),
      instanceId("sar_harness_instance", "0",
                 "Instance index N for multi-game-process RL training.\n"
                 "Derives gRPC port as 50000+N and SHM name as\n"
                 "portal2_harness_framebuffer_N.\n"),
      harnessRecord(
          "sar_harness_record", "0",
          "Enable harness demo recording alongside normal demo recording.\n"
          "When enabled, any 'record' command will also produce a .hdem "
          "sidecar.\n"
          "0 = off, 1 = record .hdem alongside .dem\n") {
  this->hasLoaded = true;
  this->rolloutRecorder = new RolloutRecorder();
  this->hdemRecorder = new HdemRecorder();
  this->entitySnapshotter = new EntitySnapshotter();
}

Harness::~Harness() {
  // Null out global pointer FIRST so event handlers bail immediately
  harness = nullptr;

  // Stop TasPlayer to prevent it from accessing our framebulk data
  if (tasPlayer && tasPlayer->IsActive()) {
    tasPlayer->Stop(true);
  }

  this->StopServer();
  if (this->hdemRecorder) {
    delete this->hdemRecorder;
  }
  if (this->entitySnapshotter) {
    delete this->entitySnapshotter;
  }
}

// docs/Harness.cpp:StartServer>
void Harness::StartServer() {
  if (this->shouldRun) return;
  this->shouldRun = true;
  this->serverThread = std::thread([this] {
    int instanceN = this->instanceId.GetInt();
    int port = 50000 + instanceN;
    std::string server_address = "0.0.0.0:" + std::to_string(port);
    console->Print(
        "Harness: gRPC server thread started (instance=%d, port=%d)\n",
        instanceN, port);
    Portal2HarnessImpl service;

    grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    this->server = builder.BuildAndStart();
    if (this->server) {
      console->Print("Harness gRPC server listening on %s\n",
                     server_address.c_str());
      while (this->shouldRun) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
      // server->Shutdown() is called by StopServer()
    } else {
      console->Warning("Failed to start Harness gRPC server!\n");
      this->shouldRun = false;
    }
  });
}

void Harness::StopServer() {
  if (!this->shouldRun) return;
  this->shouldRun = false;

  // Wake up any threads blocked on condvars (Act, Reset)
  this->harnessControlActive = false;
  this->ticksRemaining = 0;
  this->warmupTicksRemaining = 0;
  {
    std::lock_guard<std::mutex> lock(this->tickMutex);
    this->tickCV.notify_all();
  }
  {
    std::lock_guard<std::mutex> lock(this->resetMutex);
    this->resetCV.notify_all();
  }

  // Shut down the gRPC server to terminate any in-flight RPCs
  if (this->server) {
    auto deadline = std::chrono::system_clock::now() + std::chrono::seconds(1);
    this->server->Shutdown(deadline);
    this->server.reset();
  }

  // Detach the server thread — during process exit, joining can hang
  // if gRPC internals are still cleaning up
  if (this->serverThread.joinable()) {
    this->serverThread.detach();
  }
}

// ================================================================
// Event handlers
// ================================================================

// SESSION_START: When a session begins with harness enabled, activate TasPlayer
ON_EVENT(SESSION_START) {
  if (harness && harness->entitySnapshotter) {
    harness->entitySnapshotter->DiscoverSchema();
  }
  if (!harness || !harness->IsEnabled() || harness->isRecordingRollout) return;

  console->Print(
      "Harness: Session started, activating TasPlayer and starting %d warmup "
      "ticks...\n",
      HARNESS_WARMUP_TICKS);

  ActivateHarnessTasPlayer();
  harness->warmupTicksRemaining = HARNESS_WARMUP_TICKS;
  harness->harnessControlActive = false;
}

void Harness::RecordDemoAction(const CUserCmd& cmd) {
  this->lastDemoAction = cmd;
}

// PRE_TICK: Manage warmup countdown and tick synchronization
ON_EVENT(PRE_TICK) {
  if (!harness || !harness->IsEnabled() || harness->isRecordingRollout) return;
  if (!harness->harnessControlActive && harness->warmupTicksRemaining <= 0)
    return;

  // Warmup phase: let the game run freely while TasPlayer initializes
  if (harness->warmupTicksRemaining > 0) {
    harness->warmupTicksRemaining--;
    if (harness->warmupTicksRemaining == 0) {
      console->Print(
          "Harness: Warmup complete, pausing game and waiting for Act "
          "calls...\n");
      engine->SetAdvancing(true);  // Pause the game
      harness->harnessControlActive = true;

      // Notify Reset() if it's waiting for warmup completion
      {
        std::lock_guard<std::mutex> lock(harness->resetMutex);
        harness->resetCV.notify_one();
      }
    }
    return;
  }

  // Act tick counting: decrement remaining ticks and notify when done
  if (harness->ticksRemaining > 0) {
    harness->ticksRemaining--;
    if (harness->ticksRemaining <= 0) {
      // All requested ticks have executed, notify the waiting Act() call
      std::lock_guard<std::mutex> lock(harness->tickMutex);
      harness->tickCV.notify_one();
    }
  }
}

// POST_TICK: Drive active sidecar recording stream tick-by-tick
ON_EVENT(POST_TICK) {
  if (!harness || !harness->entitySnapshotter) return;

  bool hdemActive = harness->hdemRecorder && harness->hdemRecorder->IsActive();
  bool harnessEnabled = harness->IsEnabled();

  if (!hdemActive && !harnessEnabled) return;

  // Ensure game engine structures are fully initialized
  if (!engine || !engine->hoststate || !server || !server->gpGlobals) {
    return;
  }

  // Only record state frames when server simulation is actively ticking a live
  // map
  if (!engine->hoststate->m_activeGame) {
    return;
  }

  harness->entitySnapshotter->Update();

  if (hdemActive) {
    harness->hdemRecorder->RecordTick(server->gpGlobals->tickcount);
  }
}

// POST_TICK: Record rollout data during demo playback
ON_EVENT(POST_TICK) {
  if (!harness || !harness->isRecordingRollout ||
      !harness->rolloutRecorder->IsActive())
    return;

  if (!engine->demoplayer->IsPlaying()) return;

  harness->wasPlayingDemo = true;

  portal2_harness::GameState state;
  Portal2HarnessImpl impl;
  if (impl.InternalObserve(&state)) {
    portal2_harness::ActionRequest action;
    harness->rolloutRecorder->MapUserCmdToAction(harness->lastDemoAction,
                                                 &action);

    void* pixels = nullptr;
    size_t pixelSize = 0;
    if (harness->rolloutRecorder->CapturesPixels() && g_harness_videomode_ptr &&
        *g_harness_videomode_ptr) {
      void* videomode = *g_harness_videomode_ptr;
      pixels = harness->rolloutRecorder->GetBuffer();
      pixelSize = harness->rolloutRecorder->GetBufferSize();
      int sw = 854;
      int sh = 480;
      if (engine && engine->GetScreenSize) {
        engine->GetScreenSize(nullptr, sw, sh);
      }
      Memory::VMT<void(__rescall*)(void*, int, int, int, int, void*, int)>(
          videomode, Offsets::ReadScreenPixels)(videomode, 0, 0, sw, sh, pixels,
                                                2 /* IMAGE_FORMAT_RGB888 */);
    }

    harness->rolloutRecorder->RecordTick(state, action, pixels, pixelSize);

    if (harness->rolloutRecorder->recordedTicks % 500 == 0) {
      console->Print("Harness: Recorded %zu ticks (%zu bytes)...\n",
                     harness->rolloutRecorder->recordedTicks,
                     harness->rolloutRecorder->totalBytes);
    }
  }
}

ON_EVENT(SESSION_END) {
  if (harness && harness->hdemRecorder && harness->hdemRecorder->IsActive()) {
    harness->hdemRecorder->Stop();
  }
}

ON_EVENT(DEMO_STOP) {
  if (harness && harness->hdemRecorder && harness->hdemRecorder->IsActive()) {
    harness->hdemRecorder->Stop();
  }
}

// Manual stop command
CON_COMMAND(
    sar_harness_stop_rollout,
    "sar_harness_stop_rollout - Stops the current rollout recording.\n") {
  if (harness && harness->isRecordingRollout) {
    size_t ticks = harness->rolloutRecorder->recordedTicks;
    size_t bytes = harness->rolloutRecorder->totalBytes;
    harness->rolloutRecorder->Stop();
    {
      std::lock_guard<std::mutex> lock(harness->recordingMutex);
      harness->isRecordingRollout = false;
      harness->wasPlayingDemo = false;
      harness->recordingCV.notify_all();
    }
    console->Print(
        "Harness: Stopped recording. Total: %zu ticks (%zu bytes).\n", ticks,
        bytes);
  } else {
    console->Print("Harness: No rollout recording active.\n");
  }
}

// Automatically stop recording when demo playback ends
ON_EVENT(DEMO_STOP) {
  if (harness && harness->isRecordingRollout) {
    size_t ticks = harness->rolloutRecorder->recordedTicks;
    size_t bytes = harness->rolloutRecorder->totalBytes;
    harness->rolloutRecorder->Stop();
    {
      std::lock_guard<std::mutex> lock(harness->recordingMutex);
      harness->isRecordingRollout = false;
      harness->wasPlayingDemo = false;
      harness->recordingCV.notify_all();
    }
    console->Print(
        "Harness: Demo playback finished. Stopped recording. Total: %zu ticks "
        "(%zu bytes).\n",
        ticks, bytes);
  }
}

DECL_COMMAND_FILE_COMPLETION(sar_harness_playdemo, ".dem", "", 1);
CON_COMMAND_F_COMPLETION(
    sar_harness_playdemo,
    "sar_harness_playdemo <demo> [output] [pixels:0|1] - Plays a demo and "
    "records a .rollout file.\n",
    0, AUTOCOMPLETION_FUNCTION(sar_harness_playdemo)) {
  if (args.ArgC() < 2) {
    return console->Print(sar_harness_playdemo.ThisPtr()->m_pszHelpString);
  }

  std::string demoPath = args[1];
  std::string outputPath =
      (args.ArgC() >= 3) ? args[2] : (demoPath + ".rollout");
  bool capturePixels =
      (args.ArgC() >= 4) ? (std::string(args[3]) == "1") : false;

  if (!Utils::EndsWith(demoPath, ".dem")) demoPath += ".dem";
  if (!Utils::EndsWith(outputPath, ".rollout")) outputPath += ".rollout";

  // Ensure absolute path in the game directory if it's just a filename
  if (outputPath.find('/') == std::string::npos &&
      outputPath.find('\\') == std::string::npos) {
    outputPath = std::string(engine->GetGameDirectory()) + "/" + outputPath;
  }

  // Check if demo file exists
  auto fullPath = fileSystem->FindFileSomewhere(demoPath).value_or(demoPath);
  if (!std::filesystem::exists(fullPath)) {
    return console->Warning("Harness: Demo file not found: %s\n",
                            demoPath.c_str());
  }

  console->Print("Harness: Starting rollout recording to %s\n",
                 outputPath.c_str());

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

  int sw = 854;
  int sh = 480;
  if (engine && engine->GetScreenSize) {
    engine->GetScreenSize(nullptr, sw, sh);
  }

  if (!harness->rolloutRecorder->Start(outputPath, targetMapName, shmName, sw,
                                       sh, 1.0f / engine->GetIPT(),
                                       capturePixels)) {
    return console->Warning(
        "Harness: Failed to open rollout file for writing!\n");
  }

  {
    std::lock_guard<std::mutex> lock(harness->recordingMutex);
    harness->isRecordingRollout = true;
    harness->wasPlayingDemo = false;
  }

  // Execute playdemo
  std::string cmd =
      "sar_disable_challenge_stats_hud -1; hideconsole; playdemo \"" +
      demoPath + "\"";
  engine->ExecuteCommand(cmd.c_str(), true);
}
