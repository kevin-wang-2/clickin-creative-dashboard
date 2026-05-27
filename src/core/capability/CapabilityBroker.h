#pragma once

#include "core/capability/CapabilityRegistry.h"
#include "core/capability/CapabilityFuture.h"
#include "sdk/CapabilityCodec.h"

namespace clickin {

class CapabilityBroker {
public:
    explicit CapabilityBroker(CapabilityRegistry& registry);

    // Non-template core dispatch — lives in .cpp.
    CapabilityFuture<RawResult>
    invokeRaw(const CapabilityRef& ref, RawRequest request);

    // Find the highest-priority available handler for a capability.
    CapabilityRef findBest(std::string_view capability, int version,
                           const CapabilityQuery& query) const;

    // Typed convenience wrappers — header-only, thin encode/decode shell.
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
            [](const RawResult& rawResult) {
                return CapabilityCodec<Contract>::decodeResult(rawResult);
            }
        );
    }

private:
    CapabilityRegistry& registry_;
};

} // namespace clickin
