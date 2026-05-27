#pragma once

#include "core/capability/CapabilityRegistry.h"
#include "core/capability/CapabilityFuture.h"
#include "sdk/CapabilityCodec.h"

namespace clickin {

class CapabilityBroker {
public:
    explicit CapabilityBroker(CapabilityRegistry& registry);

    // ── Raw dispatch (non-template, lives in .cpp) ────────────────────────────
    // Performs: enabled check → describe() availability check → handler invoke.
    // All error paths return a resolved CapabilityFuture with ok=false.
    CapabilityFuture<RawResult>
    invokeRaw(const CapabilityRef& ref, RawRequest request);

    // Find the highest-priority *available and enabled* handler.
    // Returns an invalid CapabilityRef (valid()==false) if none found.
    CapabilityRef findBest(std::string_view capability, int version,
                           const CapabilityQuery& query) const;

    // ── Typed wrappers (header-only, thin shell) ──────────────────────────────

    template <typename Contract>
    CapabilityRef findBest(const CapabilityQuery& query) const {
        return findBest(Contract::capability, Contract::version, query);
    }

    template <typename Contract>
    CapabilityFuture<typename Contract::Result>
    invoke(const CapabilityRef& ref, const typename Contract::Request& request) {
        RawRequest raw = CapabilityCodec<Contract>::encodeRequest(request);
        raw.providerId = ref.providerId;

        return invokeRaw(ref, std::move(raw)).then(
            [](const RawResult& rawResult) -> typename Contract::Result {
                if (!rawResult.ok) {
                    // Propagate the error as a default-constructed result.
                    // Callers should check the RawResult directly for errors;
                    // this path is reached only when callers use .then() chains.
                    return {};
                }
                return CapabilityCodec<Contract>::decodeResult(rawResult);
            }
        );
    }

    // Convenience: findBest + invoke in one call.
    template <typename Contract>
    CapabilityFuture<typename Contract::Result>
    call(const CapabilityQuery& query, const typename Contract::Request& request) {
        auto ref = findBest<Contract>(query);
        return invoke<Contract>(ref, request);
    }

    CapabilityRegistry& registry() { return registry_; }

private:
    static RawResult makeError(std::string_view capability, int version,
                                std::string_view errorCode,
                                std::string_view message);

    CapabilityRegistry& registry_;
};

} // namespace clickin
