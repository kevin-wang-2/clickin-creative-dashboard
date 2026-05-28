#pragma once

#include <string_view>

namespace clickin {

struct CapabilityQuery;
struct CapabilityDescriptor;
struct RawRequest;
struct RawResult;
class CapabilityContext;

template <typename T>
class CapabilityFuture;

// How the broker dispatches invocations of this handler.
// See CONTRIBUTING.md "Handler execution policies".
enum class ExecutionPolicy {
    Sync,       // Run inline on the calling thread. Only for built-in plugins.
    Async,      // Dispatch to WorkerPool (default for all handlers).
    Subprocess  // IPC round-trip (not yet implemented — treated as Async).
};

class IRawCapabilityHandler {
public:
    virtual ~IRawCapabilityHandler() = default;

    virtual std::string_view capabilityName() const = 0;
    virtual int              capabilityVersion() const = 0;
    virtual std::string_view providerId() const = 0;

    virtual CapabilityDescriptor describe(const CapabilityQuery& query) = 0;

    virtual CapabilityFuture<RawResult>
    invokeRaw(const RawRequest& request, CapabilityContext& ctx) = 0;

    // Default: Async. Override to Sync for fast built-in handlers.
    virtual ExecutionPolicy executionPolicy() const { return ExecutionPolicy::Async; }
};

} // namespace clickin
