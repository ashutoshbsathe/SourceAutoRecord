#pragma once
#include <grpcpp/grpcpp.h>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

#include "Command.hpp"
#include "Features/Feature.hpp"
#include "HarnessShm.hpp"
#include "Variable.hpp"
#include "harness.grpc.pb.h"
#include "harness.pb.h"

// docs/Harness.hpp:Harness>
class Harness : public Feature {
 public:
  Harness();
  ~Harness();

  void StartServer();
  void StopServer();

  bool IsEnabled() { return enabled.GetBool(); }
  int GetInstanceId() { return instanceId.GetInt(); }

  // Synchronization for Act() <-> PRE_TICK
  std::mutex tickMutex;
  std::condition_variable tickCV;
  std::atomic<int> ticksRemaining{0};
  std::atomic<bool> harnessControlActive{
      false};  // true when harness owns input

  // Synchronization for Reset() <-> warmup completion
  std::mutex resetMutex;
  std::condition_variable resetCV;

  // Warmup state
  std::atomic<int> warmupTicksRemaining{0};

  // Rollout Recording
  void RecordDemoAction(const CUserCmd& cmd);
  std::atomic<bool> isRecordingRollout{false};
  std::mutex recordingMutex;
  std::condition_variable recordingCV;
  bool wasPlayingDemo = false;
  CUserCmd lastDemoAction;
  class RolloutRecorder* rolloutRecorder = nullptr;
  class HdemRecorder* hdemRecorder = nullptr;
  class EntitySnapshotter* entitySnapshotter = nullptr;

  Variable enabled;
  Variable instanceId;  // sar_harness_instance: integer N → port 50000+N, shm
                        // suffix _N
  Variable harnessRecord;

 private:
  std::unique_ptr<grpc::Server> server;
  std::thread serverThread;
  std::atomic<bool> shouldRun{false};
};

void Portal2Harness_InitVideoMode(void** videomode);

extern Harness* harness;

class Portal2HarnessImpl final
    : public portal2_harness::Portal2Harness::Service {
 public:
  Portal2HarnessImpl();

  grpc::Status InitialHandshake(
      grpc::ServerContext* context,
      const portal2_harness::HandshakeRequest* request,
      portal2_harness::HandshakeResponse* response) override;
  grpc::Status Observe(grpc::ServerContext* context,
                       const portal2_harness::Empty* request,
                       portal2_harness::GameState* response) override;
  grpc::Status Act(grpc::ServerContext* context,
                   const portal2_harness::ActionRequest* request,
                   portal2_harness::ActionResponse* response) override;
  grpc::Status ExecuteCommand(
      grpc::ServerContext* context,
      const portal2_harness::CommandRequest* request,
      portal2_harness::CommandResponse* response) override;
  grpc::Status Reset(grpc::ServerContext* context,
                     const portal2_harness::ResetRequest* request,
                     portal2_harness::ResetResponse* response) override;

  grpc::Status AgentLoop(
      grpc::ServerContext* context,
      grpc::ServerReaderWriter<portal2_harness::EnvironmentMessage,
                               portal2_harness::AgentMessage>* stream) override;

  grpc::Status RenderDemo(
      grpc::ServerContext* context,
      const portal2_harness::RenderDemoRequest* request,
      portal2_harness::RenderDemoResponse* response) override;

  bool InternalObserve(portal2_harness::GameState* response);

 private:
  bool playerDied = false;
  HarnessShm shm;
};

extern Command sar_harness_playdemo;
