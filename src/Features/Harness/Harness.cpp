#include "Harness.hpp"

#include "Event.hpp"
#include "Features/Session.hpp"
#include "Features/Tas/TasController.hpp"
#include "Features/Tas/TasPlayer.hpp"
#include "Features/Tas/TasScript.hpp"
#include "Modules/Console.hpp"
#include "Modules/Engine.hpp"
#include "Modules/Server.hpp"
#include "SAR.hpp"
#include "Utils/SDK.hpp"

#include <string>

Harness *harness;

static int g_harness_warmup_ticks_remaining = 0;
static int g_harness_current_tick = 0;
const int HARNESS_WARMUP_TICKS = 256;

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

static void sar_harness_callback(void *var, const char *pOldValue, float flOldValue) {
	if (harness->IsEnabled()) {
		console->Print("Harness enabled. Initializing gRPC server thread...\n");
		harness->StartServer();
	} else {
		console->Print("Harness disabled. Shutting down gRPC server...\n");

		// Stop TasPlayer and return control to human
		if (tasPlayer && tasPlayer->IsActive()) {
			tasPlayer->Stop();
		}
		g_harness_warmup_ticks_remaining = 0;

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
}

Portal2HarnessImpl::Portal2HarnessImpl()
	: playerDied(false) {
	g_harness_current_tick = 0;
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

	// Handle player death - restart map
	if (health <= 0) {
		console->Print("Harness: Player is dead, restarting level...\n");
		engine->ExecuteCommand("restart_level");
		g_harness_current_tick = 0;
		playerDied = false;
		engine->SetAdvancing(false);  // Unpause the game
	}

	return grpc::Status::OK;
}

grpc::Status Portal2HarnessImpl::Act(grpc::ServerContext *context, const portal2_harness::ActionRequest *request, portal2_harness::ActionResponse *response) {
	// if (!session->isRunning) {
	// 	response->set_success(false);
	// 	response->set_error_message("No session running");
	// 	return grpc::Status::OK;
	// }
	//
	// if (!tasPlayer || !tasPlayer->IsActive()) {
	// 	response->set_success(false);
	// 	response->set_error_message("TasPlayer not active");
	// 	return grpc::Status::OK;
	// }
	//
	// int numTicks = request->num_ticks();
	// if (numTicks <= 0) numTicks = 1;
	//
	// // Convert ActionRequest to TasFramebulk
	// TasFramebulk fb;
	// fb.tick = g_harness_current_tick;
	//
	// // Map movement keys to moveAnalog
	// Vector moveAnalog = {0, 0};
	// if (request->key_forward()) moveAnalog.y += 1.0f;
	// if (request->key_backward()) moveAnalog.y -= 1.0f;
	// if (request->key_right()) moveAnalog.x += 1.0f;
	// if (request->key_left()) moveAnalog.x -= 1.0f;
	// // Normalize if diagonal
	// if (moveAnalog.Length2D() > 1.0f) {
	// 	moveAnalog = moveAnalog.Normalize();
	// }
	// fb.moveAnalog = moveAnalog;
	//
	// // Map mouse movement to viewAnalog
	// fb.viewAnalog.x = request->mouse_dx();
	// fb.viewAnalog.y = request->mouse_dy();
	//
	// // Map button states
	// for (int i = 0; i < TAS_CONTROLLER_INPUT_COUNT; i++) {
	// 	fb.buttonStates[i] = false;
	// }
	//
	// // Map proto fields to TAS button indices based on TasControllerInput enum
	// // Jump=0, Crouch=1, Use=2, Zoom=3, FireBlue=4, FireOrange=5, Sprint=6, Reload=7, Flashlight=8
	// if (request->key_jump()) fb.buttonStates[Jump] = true;
	// if (request->key_crouch()) fb.buttonStates[Crouch] = true;
	// if (request->key_use()) fb.buttonStates[Use] = true;
	// if (request->key_zoomin()) fb.buttonStates[Zoom] = true;
	// if (request->key_zoomout()) fb.buttonStates[Zoom] = true;  // Both zoom in/out map to same Zoom button
	// if (request->portal_primary()) fb.buttonStates[FireBlue] = true;
	// if (request->portal_secondary()) fb.buttonStates[FireOrange] = true;
	//
	// // Hijack input control for the duration of Act execution
	// int old_forceuser = in_forceuser.GetInt();
	// in_forceuser.SetValue(engine->GetMaxClients() + 1);
	// tasControllers[0]->Enable();
	//
	// // Add framebulks to TasPlayer's playback queue and advance frame-by-frame
	// for (int i = 0; i < numTicks; i++) {
	// 	TasFramebulk tickFb = fb;
	// 	tickFb.tick = g_harness_current_tick + 1;  // FetchInputs looks for g_harness_current_tick + 1
	// 	tasPlayer->playbackInfo.slots[0].framebulks.push_back(tickFb);
	//
	// 	// Manually unpause, advance, re-pause
	// 	engine->SetAdvancing(false);
	// 	engine->AdvanceTick();
	// 	engine->SetAdvancing(true);
	// 	g_harness_current_tick++;
	//
	// 	// Check if player died
	// 	void *player = server->GetPlayer(1);
	// 	if (player) {
	// 		ServerEnt *pl = (ServerEnt *)player;
	// 		int health = pl->field<int>("m_iHealth");
	// 		if (health <= 0) {
	// 			playerDied = true;
	// 			break;  // Stop executing remaining ticks
	// 		}
	// 	}
	// }
	//
	// // Restore input control back to human
	// tasControllers[0]->Disable();
	// in_forceuser.SetValue(old_forceuser);
	//
	// response->set_success(true);
	return grpc::Status::OK;
}
//
// // SESSION_START event handler for Harness - initialize TasPlayer when session starts
// ON_EVENT(SESSION_START) {
// 	if (harness && harness->IsEnabled()) {
// 		// Initialize TasPlayer with empty playback when session starts
// 		TasPlaybackInfo info;
// 		// Need at least one framebulk for Activate() to set active=true
// 		TasFramebulk dummy;
// 		dummy.tick = -1;
// 		info.slots[0].framebulks.push_back(dummy);
// 		info.slots[0].header.version = 8;
// 		info.slots[0].header.startInfo.type = StartImmediately;
// 		info.slots[0].header.startInfo.isNext = false;
//
// 		tasPlayer->Activate(info);
// 		tasPlayer->Start();
//
// 		// Revert the input capture that Start() just did
// 		in_forceuser.SetValue(0);
// 		tasControllers[0]->Disable();
//
// 		g_harness_current_tick = 1;  // Match TasPlayer off-by-one logic
// 		// Set warmup counter
// 		g_harness_warmup_ticks_remaining = HARNESS_WARMUP_TICKS;
// 		console->Print("Harness: Session started, running %d warmup ticks...\n", HARNESS_WARMUP_TICKS);
// 	}
// }
//
// // PRE_TICK event handler for warmup tick countdown
// ON_EVENT(PRE_TICK) {
// 	if (harness && harness->IsEnabled() && g_harness_warmup_ticks_remaining > 0) {
// 		g_harness_warmup_ticks_remaining--;
// 		if (g_harness_warmup_ticks_remaining == 0) {
// 			console->Print("Harness: Warmup complete, pausing game and waiting for Act calls...\n");
// 			engine->SetAdvancing(true);  // Manually pause since we didn't call Start()
// 		}
// 	}
// }
// 		console->Print("Harness enabled, freezing game...\n");
// 		engine->SetAdvancing(true);
// 	}
// }
