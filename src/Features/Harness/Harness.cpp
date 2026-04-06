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

// Everything below (the stubs) are added by Gemini 3 Flash to fix some compilation warnings
// Renderer stubs to avoid FFMPEG dependency
namespace Renderer {
	int segmentEndTick = -1;
	bool isDemoLoading = false;
	void Frame() {}
	void Init(void **videomode) {}
	void Cleanup() {}
	bool IsRunning() {
		return false;
	}
}  // namespace Renderer

extern "C" {
// --- Systematic Compatibility Bridge ---

// --- OpenSSL 3.x Compatibility Stubs ---
// These are needed because libgrpc.a was built against OpenSSL 3.x headers,
// but we are linking against OpenSSL 1.1 from lib/curl.
// Since we use InsecureServerCredentials, these shouldn't be called at runtime.

void *EVP_MAC_fetch(void *ctx, const char *algorithm, const char *properties) {
	return nullptr;
}
void EVP_MAC_free(void *mac) {}
void *EVP_MAC_CTX_new(void *mac) {
	return nullptr;
}
void EVP_MAC_CTX_free(void *ctx) {}
int EVP_MAC_init(void *ctx, const unsigned char *key, size_t keylen, void *params) {
	return 0;
}
int EVP_MAC_update(void *ctx, const unsigned char *data, size_t datalen) {
	return 0;
}
int EVP_MAC_final(void *ctx, unsigned char *out, size_t *outlen, size_t outsize) {
	return 0;
}
int EVP_Q_digest(void *libctx, const char *name, const char *propq, const void *data, size_t datalen, unsigned char *md, size_t *mdlen) {
	return 0;
}
int EVP_DigestSignUpdate(void *ctx, const void *data, size_t dlen) {
	return 0;
}
void *OSSL_PARAM_construct_utf8_string(const char *key, char *buf, size_t bsize) {
	return nullptr;
}
void *OSSL_PARAM_construct_end(void) {
	return nullptr;
}
void *SSL_get1_peer_certificate(const void *s) {
	return nullptr;
}

// Systemd Stubs
int sd_listen_fds(int unset_environment) {
	return 0;
}
int sd_is_socket_inet(int fd, int family, int type, int listening, unsigned short port) {
	return 0;
}
int sd_is_socket_unix(int fd, int type, int listening, const char *path, unsigned int length) {
	return 0;
}
int sd_is_socket_sockaddr(int fd, int type, const void *addr, unsigned int addrlen, int listening) {
	return 0;
}
}

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
			this->server->Shutdown();
		} else {
			console->Warning("Failed to start Harness gRPC server!\n");
			this->shouldRun = false;
		}
	});
}

void Harness::StopServer() {
	if (!this->shouldRun) return;
	this->shouldRun = false;
	if (this->serverThread.joinable()) {
		this->serverThread.join();
	}
	if (this->harnessControlActive) {
		this->harnessControlActive = false;
		Scheduler::OnMainThread([]() {
			engine->SetAdvancing(false);
		});
	}
}

Portal2HarnessImpl::Portal2HarnessImpl()
	: playerDied(false) {
}

grpc::Status Portal2HarnessImpl::InitialHandshake(grpc::ServerContext *context, const portal2_harness::HandshakeRequest *request, portal2_harness::HandshakeResponse *response) {
	std::string version = sar.game->Version();
	std::string map = engine->GetCurrentMapName();
	console->Print("Harness: InitialHandshake called. Responding with: version=%s, map=%s\n", version.c_str(), map.c_str());
	response->set_game_version(version);
	response->set_map_name(map);
	return grpc::Status::OK;
}

grpc::Status Portal2HarnessImpl::Observe(grpc::ServerContext *context, const portal2_harness::Empty *request, portal2_harness::GameState *response) {
	if (!session->isRunning) {
		return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, "No session running");
	}

	// Get player entity (slot 0, index 1)
	void *player = server->GetPlayer(1);
	if (!player) {
		return grpc::Status(grpc::StatusCode::INTERNAL, "Failed to get player entity");
	}

	ServerEnt *pl = (ServerEnt *)player;

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

grpc::Status Portal2HarnessImpl::Act(grpc::ServerContext *context, const portal2_harness::ActionRequest *request, portal2_harness::ActionResponse *response) {
	if (!session->isRunning) {
		response->set_success(false);
		response->set_error_message("No session running");
		return grpc::Status::OK;
	}

	if (!harness->harnessControlActive) {
		response->set_success(false);
		response->set_error_message("Harness control not active (warmup may not be complete)");
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
		TasFramebulk &fb = tasPlayer->playbackInfo.slots[0].framebulks[0];
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
		harness->tickCV.wait(lock, []() {
			return harness->ticksRemaining <= 0;
		});
	}

	response->set_success(true);
	return grpc::Status::OK;
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
