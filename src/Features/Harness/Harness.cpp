#include "Harness.hpp"

#include "Event.hpp"
#include "Features/Session.hpp"
#include "Features/Tas/TasController.hpp"
#include "Features/Tas/TasPlayer.hpp"
#include "Features/Tas/TasScript.hpp"
#include "Modules/Client.hpp"
#include "Modules/Console.hpp"
#include "Modules/Engine.hpp"
#include "Modules/Server.hpp"
#include "SAR.hpp"
#include "Scheduler.hpp"
#include "Utils/SDK.hpp"

#include <array>
#include <climits>
#include <string>

Harness *harness;

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

static void sar_harness_callback(void *var, const char *pOldValue, float flOldValue) {
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
			Scheduler::OnMainThread([]() {
				tasPlayer->Stop(true);
			});
		}
		harness->StopServer();
	}
}

Harness::Harness()
	: enabled("sar_harness", "0", "Enables the Harness feature.\n", 0, sar_harness_callback) {
	this->hasLoaded = true;
}

Harness::~Harness() {
	// Null out global pointer FIRST so event handlers bail immediately
	harness = nullptr;

	// Stop TasPlayer to prevent it from accessing our framebulk data
	if (tasPlayer && tasPlayer->IsActive()) {
		tasPlayer->Stop(true);
	}

	this->StopServer();
}

void Harness::StartServer() {
	if (this->shouldRun) return;
	this->shouldRun = true;
	this->serverThread = std::thread([this] {
		console->Print("Harness: gRPC server thread started\n");
		std::string server_address("0.0.0.0:50051");
		Portal2HarnessImpl service;

		grpc::ServerBuilder builder;
		builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
		builder.RegisterService(&service);
		this->server = builder.BuildAndStart();
		if (this->server) {
			console->Print("Harness gRPC server listening on %s\n", server_address.c_str());
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
	if (!harness || !harness->IsEnabled()) return;

	console->Print("Harness: Session started, activating TasPlayer and starting %d warmup ticks...\n", HARNESS_WARMUP_TICKS);

	ActivateHarnessTasPlayer();
	harness->warmupTicksRemaining = HARNESS_WARMUP_TICKS;
	harness->harnessControlActive = false;
}

// PRE_TICK: Manage warmup countdown and tick synchronization
ON_EVENT(PRE_TICK) {
	if (!harness || !harness->IsEnabled()) return;
	if (!harness->harnessControlActive && harness->warmupTicksRemaining <= 0) return;

	// Warmup phase: let the game run freely while TasPlayer initializes
	if (harness->warmupTicksRemaining > 0) {
		harness->warmupTicksRemaining--;
		if (harness->warmupTicksRemaining == 0) {
			console->Print("Harness: Warmup complete, pausing game and waiting for Act calls...\n");
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
