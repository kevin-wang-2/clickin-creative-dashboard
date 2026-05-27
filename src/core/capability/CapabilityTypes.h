#pragma once

#include <string>
#include <string_view>
#include "core/types/RawPayload.h"

namespace clickin {

// ── Capability identity ───────────────────────────────────────────────────────

struct CapabilityRef {
    ProviderId  providerId;
    std::string capability;
    int         version = 0;

    bool valid() const { return !providerId.empty() && !capability.empty(); }
};

struct CapabilityQuery {
    AssetId     assetId;
    std::string capability;
    int         version = 0;
};

// ── Descriptor returned by IRawCapabilityHandler::describe() ─────────────────

enum class ExecutionKind { Sync, Async, Job };

struct CapabilityDescriptor {
    bool          available        = false;
    int           priority         = 0;
    bool          longRunning      = false;
    bool          cancellable      = false;
    ExecutionKind executionKind    = ExecutionKind::Sync;
    std::string   unavailableReason;
};

// ── Error codes (PRD §2.9) ────────────────────────────────────────────────────
// String constants so error codes survive future serialization boundaries.

namespace CapabilityError {
    inline constexpr std::string_view no_handler              = "no_handler";
    inline constexpr std::string_view handler_unavailable     = "handler_unavailable";
    inline constexpr std::string_view capability_version_mismatch = "capability_version_mismatch";
    inline constexpr std::string_view invalid_request         = "invalid_request";
    inline constexpr std::string_view invalid_result          = "invalid_result";
    inline constexpr std::string_view provider_error          = "provider_error";
    inline constexpr std::string_view timeout                 = "timeout";
    inline constexpr std::string_view cancelled               = "cancelled";
    inline constexpr std::string_view internal_error          = "internal_error";
} // namespace CapabilityError

// ── Invocation context passed to every handler ───────────────────────────────

class CapabilityBroker; // forward declaration — breaks include cycle

class CapabilityContext {
public:
    CapabilityContext() = default;
    explicit CapabilityContext(CapabilityBroker& broker) : broker_(&broker) {}

    // May be null during unit tests that construct context without a broker.
    CapabilityBroker* broker() const { return broker_; }

private:
    CapabilityBroker* broker_ = nullptr;
};

} // namespace clickin
