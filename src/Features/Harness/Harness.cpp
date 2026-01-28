#include "Harness.hpp"

#include "Event.hpp"
#include "Modules/Console.hpp"
#include "Modules/Engine.hpp"
#include "SAR.hpp"

#include <string>

Harness *harness;

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
		harness->StopServer();
		engine->SetAdvancing(false);
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

grpc::Status Portal2HarnessImpl::InitialHandshake(grpc::ServerContext *context, const portal2_harness::HandshakeRequest *request, portal2_harness::HandshakeResponse *response) {
	std::string version = sar.game->Version();
	std::string map = engine->GetCurrentMapName();
	console->Print("Harness: InitialHandshake called. Responding with: version=%s, map=%s\n", version.c_str(), map.c_str());
	response->set_game_version(version);
	response->set_map_name(map);
	return grpc::Status::OK;
}

grpc::Status Portal2HarnessImpl::Observe(grpc::ServerContext *context, const portal2_harness::Empty *request, portal2_harness::GameState *response) {
	console->Print("Harness: Observe called\n");
	// To be implemented in next step
	return grpc::Status::OK;
}

grpc::Status Portal2HarnessImpl::Act(grpc::ServerContext *context, const portal2_harness::ActionRequest *request, portal2_harness::ActionResponse *response) {
	console->Print("Harness: Act called\n");
	if (request->key_forward()) console->Print("  - Forward\n");
	if (request->key_backward()) console->Print("  - Backward\n");
	if (request->key_left()) console->Print("  - Left\n");
	if (request->key_right()) console->Print("  - Right\n");
	if (request->key_jump()) console->Print("  - Jump\n");
	if (request->portal_primary()) console->Print("  - Portal Primary\n");
	if (request->portal_secondary()) console->Print("  - Portal Secondary\n");
	if (request->mouse_dx() != 0 || request->mouse_dy() != 0) {
		console->Print("  - Mouse Move: (%f, %f)\n", request->mouse_dx(), request->mouse_dy());
	}

	// To be implemented in next step - applying these to the game
	response->set_success(true);
	return grpc::Status::OK;
}

ON_EVENT(SESSION_START) {
	if (harness && harness->IsEnabled()) {
		console->Print("Harness enabled, freezing game...\n");
		engine->SetAdvancing(true);
	}
}
