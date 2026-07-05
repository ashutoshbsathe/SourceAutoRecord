#pragma once
#include <grpcpp/grpcpp.h>

class MacroExecutor;

// Handle passed to every verb in verbs/: the owning executor (for the shared
// main-thread helpers) and the gRPC context (to abort on a dropped stream).
struct VerbContext {
  MacroExecutor* exec = nullptr;
  grpc::ServerContext* rpc = nullptr;
};
