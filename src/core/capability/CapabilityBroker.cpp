#include "core/capability/CapabilityBroker.h"

namespace clickin {

CapabilityBroker::CapabilityBroker(CapabilityRegistry& registry)
    : registry_(registry) {}

CapabilityFuture<RawResult>
CapabilityBroker::invokeRaw(const CapabilityRef& ref, RawRequest request) {
    auto* handler = registry_.findHandler(ref);
    if (!handler) {
        RawResult err;
        err.ok           = false;
        err.capability   = ref.capability;
        err.version      = ref.version;
        err.errorCode    = "no_handler";
        err.errorMessage = "No handler found for capability: " + ref.capability;
        return CapabilityFuture<RawResult>(std::move(err));
    }

    CapabilityContext ctx;
    return handler->invokeRaw(request, ctx);
}

CapabilityRef CapabilityBroker::findBest(std::string_view capability, int version,
                                          const CapabilityQuery& query) const {
    auto handlers = registry_.findHandlers(capability, version);

    IRawCapabilityHandler* best     = nullptr;
    int                    bestPrio = -1;

    for (auto* h : handlers) {
        CapabilityQuery q = query;
        auto desc = h->describe(q);
        if (desc.available && desc.priority > bestPrio) {
            bestPrio = desc.priority;
            best     = h;
        }
    }

    if (!best) return {};
    return CapabilityRef{std::string(best->providerId()),
                         std::string(capability), version};
}

} // namespace clickin
