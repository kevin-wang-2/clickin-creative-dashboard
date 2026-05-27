#pragma once

#include "sdk/IRawCapabilityHandler.h"
#include "sdk/CapabilityCodec.h"
#include "core/capability/CapabilityFuture.h"
#include "core/types/RawPayload.h"

namespace clickin {

// Adapter: plugin authors implement invokeTyped(); raw encode/decode is handled here.
template <typename Contract>
class TypedCapabilityHandler : public IRawCapabilityHandler {
public:
    std::string_view capabilityName() const final {
        return Contract::capability;
    }

    int capabilityVersion() const final {
        return Contract::version;
    }

    CapabilityFuture<RawResult>
    invokeRaw(const RawRequest& raw, CapabilityContext& ctx) final {
        auto typedRequest = CapabilityCodec<Contract>::decodeRequest(raw);
        auto typedFuture  = invokeTyped(typedRequest, ctx);
        return typedFuture.then([](const typename Contract::Result& result) {
            return CapabilityCodec<Contract>::encodeResult(result);
        });
    }

protected:
    virtual CapabilityFuture<typename Contract::Result>
    invokeTyped(const typename Contract::Request& request, CapabilityContext& ctx) = 0;
};

} // namespace clickin
