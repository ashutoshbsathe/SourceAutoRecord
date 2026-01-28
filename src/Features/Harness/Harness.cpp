#include "Harness.hpp"

#include "Event.hpp"
#include "Modules/Console.hpp"
#include "Modules/Engine.hpp"
#include "SAR.hpp"

#include <string>

Harness *harness;

static void sar_harness_callback(void *var, const char *pOldValue, float flOldValue) {
	if (harness->IsEnabled()) {
		console->Print("Harness enabled. Starting gRPC server...\n");
		harness->StartServer();
	} else {
		console->Print("Harness disabled. Stopping gRPC server...\n");
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
	this->serverThread = std::thread([this]() {
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
	console->Print("Harness: InitialHandshake called\n");
	response->set_game_version(sar.game->Version());
	response->set_map_name(engine->GetCurrentMapName());
	return grpc::Status::OK;
}

grpc::Status Portal2HarnessImpl::Observe(grpc::ServerContext *context, const portal2_harness::Empty *request, portal2_harness::GameState *response) {
	console->Print("Harness: Observe called\n");
	// To be implemented in next step
	return grpc::Status::OK;
}

grpc::Status Portal2HarnessImpl::Act(grpc::ServerContext *context, const portal2_harness::ActionRequest *request, portal2_harness::ActionResponse *response) {
	console->Print("Harness: Act called\n");
	// To be implemented in next step
	response->set_success(true);
	return grpc::Status::OK;
}

ON_EVENT(SESSION_START) {
	if (harness && harness->IsEnabled()) {
		console->Print("Harness enabled, freezing game...\n");
		engine->SetAdvancing(true);
	}
}
