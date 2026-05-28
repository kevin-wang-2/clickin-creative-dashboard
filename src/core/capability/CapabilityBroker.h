#pragma once

#include "core/capability/CapabilityRegistry.h"
#include "core/capability/CapabilityFuture.h"
#include "sdk/CapabilityCodec.h"

namespace clickin {

class WorkerPool;
class MetadataService;
class CacheService;
class AssetService;
class SettingsService;

class CapabilityBroker {
public:
    explicit CapabilityBroker(CapabilityRegistry& registry);

    // ── Service injection (called by Application after services are created) ──
    // The broker does not own these services; they must outlive the broker.
    // Application::shutdown() must destroy the WorkerPool (joining all threads)
    // before destroying the registry or services.
    struct Services {
        WorkerPool*      workerPool = nullptr;
        MetadataService* metadata   = nullptr;
        CacheService*    cache      = nullptr;
        AssetService*    assets     = nullptr;
        SettingsService* settings   = nullptr;
    };
    void setServices(Services services);

    // ── Raw dispatch (non-template, lives in .cpp) ────────────────────────────
    // Checks handler's ExecutionPolicy:
    //   Sync      — invokes inline on calling thread (fast, no allocation).
    //   Async     — posts to WorkerPool; returns an unresolved future.
    //               Falls back to inline if WorkerPool is not injected (tests).
    //   Subprocess — treated as Async until #23 is implemented.
    CapabilityFuture<RawResult>
    invokeRaw(const CapabilityRef& ref, RawRequest request);

    // Find the highest-priority *available and enabled* handler.
    CapabilityRef findBest(std::string_view capability, int version,
                           const CapabilityQuery& query) const;

    // Find *all* available and enabled handlers for a capability.
    std::vector<CapabilityRef> findAll(std::string_view capability, int version,
                                       const CapabilityQuery& query = {}) const;

    // ── Typed wrappers (header-only, thin shell) ──────────────────────────────

    template <typename Contract>
    CapabilityRef findBest(const CapabilityQuery& query = {}) const {
        return findBest(Contract::capability, Contract::version, query);
    }

    template <typename Contract>
    std::vector<CapabilityRef> findAll(const CapabilityQuery& query = {}) const {
        return findAll(Contract::capability, Contract::version, query);
    }

    template <typename Contract>
    CapabilityFuture<typename Contract::Result>
    invoke(const CapabilityRef& ref, const typename Contract::Request& request) {
        RawRequest raw = CapabilityCodec<Contract>::encodeRequest(request);
        raw.providerId = ref.providerId;

        return invokeRaw(ref, std::move(raw)).then(
            [](const RawResult& rawResult) -> typename Contract::Result {
                if (!rawResult.ok) return {};
                return CapabilityCodec<Contract>::decodeResult(rawResult);
            });
    }

    template <typename Contract>
    CapabilityFuture<typename Contract::Result>
    call(const CapabilityQuery& query, const typename Contract::Request& request) {
        auto ref = findBest<Contract>(query);
        return invoke<Contract>(ref, request);
    }

    CapabilityRegistry& registry() { return registry_; }

private:
    CapabilityContext makeContext();

    static RawResult makeError(std::string_view capability, int version,
                                std::string_view errorCode,
                                std::string_view message);

    CapabilityRegistry& registry_;
    Services            services_;
};

} // namespace clickin
