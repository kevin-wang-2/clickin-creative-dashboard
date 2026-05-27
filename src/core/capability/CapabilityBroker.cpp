#include "core/capability/CapabilityBroker.h"

namespace clickin {

CapabilityBroker::CapabilityBroker(CapabilityRegistry& registry)
    : registry_(registry) {}

// static helper
RawResult CapabilityBroker::makeError(std::string_view capability, int version,
                                       std::string_view errorCode,
                                       std::string_view message) {
    RawResult err;
    err.ok           = false;
    err.capability   = std::string(capability);
    err.version      = version;
    err.errorCode    = std::string(errorCode);
    err.errorMessage = std::string(message);
    return err;
}

CapabilityFuture<RawResult>
CapabilityBroker::invokeRaw(const CapabilityRef& ref, RawRequest request) {
    const HandlerEntry* entry = registry_.findEntry(ref);

    if (!entry) {
        return CapabilityFuture<RawResult>(makeError(
            ref.capability, ref.version,
            CapabilityError::no_handler,
            "No handler registered for provider '" + ref.providerId
                + "' capability '" + ref.capability + "'"
        ));
    }

    if (!entry->enabled) {
        return CapabilityFuture<RawResult>(makeError(
            ref.capability, ref.version,
            CapabilityError::handler_unavailable,
            "Handler for provider '" + ref.providerId + "' is disabled"
        ));
    }

    CapabilityContext ctx(*this);
    return entry->handler->invokeRaw(request, ctx);
}

CapabilityRef CapabilityBroker::findBest(std::string_view capability, int version,
                                          const CapabilityQuery& query) const {
    auto entries = registry_.findHandlers(capability, version);

    const HandlerEntry* best     = nullptr;
    int                 bestPrio = -1;

    for (const auto* e : entries) {
        if (!e->enabled) continue;

        auto desc = e->handler->describe(query);
        if (desc.available && desc.priority > bestPrio) {
            bestPrio = desc.priority;
            best     = e;
        }
    }

    if (!best) return {};
    return CapabilityRef{
        std::string(best->handler->providerId()),
        std::string(capability),
        version
    };
}

} // namespace clickin
