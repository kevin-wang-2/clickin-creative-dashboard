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

namespace CapabilityError {
    inline constexpr std::string_view no_handler                  = "no_handler";
    inline constexpr std::string_view handler_unavailable         = "handler_unavailable";
    inline constexpr std::string_view capability_version_mismatch = "capability_version_mismatch";
    inline constexpr std::string_view invalid_request             = "invalid_request";
    inline constexpr std::string_view invalid_result              = "invalid_result";
    inline constexpr std::string_view provider_error              = "provider_error";
    inline constexpr std::string_view timeout                     = "timeout";
    inline constexpr std::string_view cancelled                   = "cancelled";
    inline constexpr std::string_view internal_error              = "internal_error";
} // namespace CapabilityError

// ── Invocation context passed to every handler ───────────────────────────────
//
// Provides access to broker, WorkerPool, and core services.
// Handlers should access services through ctx rather than caching them as
// mutable member state. See CONTRIBUTING.md "Handler design: stateless handlers".

class CapabilityBroker;
class WorkerPool;
class MetadataService;
class CacheService;
class AssetService;
class SettingsService;

class CapabilityContext {
public:
    CapabilityContext() = default;

    CapabilityContext(CapabilityBroker* broker,
                      WorkerPool*       workerPool,
                      MetadataService*  metadata,
                      CacheService*     cache,
                      AssetService*     assets,
                      SettingsService*  settings)
        : broker_(broker), workerPool_(workerPool),
          metadata_(metadata), cache_(cache),
          assets_(assets), settings_(settings) {}

    // May be null during unit tests that construct the context without a broker.
    CapabilityBroker* broker()     const { return broker_; }
    WorkerPool*       workerPool() const { return workerPool_; }
    MetadataService*  metadata()   const { return metadata_; }
    CacheService*     cache()      const { return cache_; }
    AssetService*     assets()     const { return assets_; }
    SettingsService*  settings()   const { return settings_; }

private:
    CapabilityBroker* broker_     = nullptr;
    WorkerPool*       workerPool_ = nullptr;
    MetadataService*  metadata_   = nullptr;
    CacheService*     cache_      = nullptr;
    AssetService*     assets_     = nullptr;
    SettingsService*  settings_   = nullptr;
};

} // namespace clickin
