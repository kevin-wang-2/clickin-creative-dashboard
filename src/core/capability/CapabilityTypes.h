#pragma once

#include <string>
#include "core/types/RawPayload.h"

namespace clickin {

struct CapabilityRef {
    ProviderId  providerId;
    std::string capability;
    int         version = 0;
};

struct CapabilityQuery {
    AssetId     assetId;
    std::string capability;
    int         version = 0;
};

enum class ExecutionKind { Sync, Async, Job };

struct CapabilityDescriptor {
    bool          available  = false;
    int           priority   = 0;
    bool          longRunning = false;
    bool          cancellable = false;
    ExecutionKind executionKind = ExecutionKind::Sync;
    std::string   unavailableReason;
};

class CapabilityContext {
public:
    // Placeholder — Phase 2 will add broker access, cancellation tokens, etc.
};

} // namespace clickin
