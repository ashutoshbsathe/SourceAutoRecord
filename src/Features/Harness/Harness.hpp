#pragma once
#include "Command.hpp"
#include "Features/Feature.hpp"
#include "Variable.hpp"
#include "harness.grpc.pb.h"
#include "harness.pb.h"

#include <atomic>
#include <grpcpp/grpcpp.h>
#include <memory>
#include <thread>

class Harness : public Feature {
public:
	Harness();
	~Harness();

	void StartServer();
	void StopServer();

	bool IsEnabled() { return enabled.GetBool(); }

private:
	Variable enabled;
	std::unique_ptr<grpc::Server> server;
	std::thread serverThread;
	std::atomic<bool> shouldRun{false};
};

extern Harness *harness;

class Portal2HarnessImpl final : public portal2_harness::Portal2Harness::Service {
	grpc::Status InitialHandshake(grpc::ServerContext *context, const portal2_harness::HandshakeRequest *request, portal2_harness::HandshakeResponse *response) override;
	grpc::Status Observe(grpc::ServerContext *context, const portal2_harness::Empty *request, portal2_harness::GameState *response) override;
	grpc::Status Act(grpc::ServerContext *context, const portal2_harness::ActionRequest *request, portal2_harness::ActionResponse *response) override;
};
