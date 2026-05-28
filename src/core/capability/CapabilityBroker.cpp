#include "core/capability/CapabilityBroker.h"
#include "core/capability/CapabilityTypes.h"
#include "core/worker/WorkerPool.h"
#include "sdk/IRawCapabilityHandler.h"

namespace clickin {

CapabilityBroker::CapabilityBroker(CapabilityRegistry& registry)
    : registry_(registry) {}

void CapabilityBroker::setServices(Services services) {
    services_ = services;
}

// static
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

CapabilityContext CapabilityBroker::makeContext() {
    return CapabilityContext(this,
                             services_.workerPool,
                             services_.metadata,
                             services_.cache,
                             services_.assets,
                             services_.settings);
}

CapabilityFuture<RawResult>
CapabilityBroker::invokeRaw(const CapabilityRef& ref, RawRequest request) {
    const HandlerEntry* entry = registry_.findEntry(ref);

    if (!entry) {
        return CapabilityFuture<RawResult>(makeError(
            ref.capability, ref.version,
            CapabilityError::no_handler,
            "No handler registered for provider '" + ref.providerId
                + "' capability '" + ref.capability + "'"));
    }

    if (!entry->enabled) {
        return CapabilityFuture<RawResult>(makeError(
            ref.capability, ref.version,
            CapabilityError::handler_unavailable,
            "Handler for provider '" + ref.providerId + "' is disabled"));
    }

    CapabilityContext ctx = makeContext();
    auto* handler = entry->handler.get();

    // Async (and Subprocess, which is treated as Async until #23) handlers are
    // posted to the WorkerPool. If WorkerPool is not injected (unit test context),
    // fall back to inline execution so tests work without a pool.
    bool useAsync = (entry->handler->executionPolicy() != ExecutionPolicy::Sync)
                    && services_.workerPool != nullptr;

    if (useAsync) {
        auto [future, resolve] = CapabilityFuture<RawResult>::makeAsync();
        services_.workerPool->post(
            [handler, req = std::move(request), ctx,
             resolve = std::move(resolve)]() mutable {
                // The handler's invokeRaw may be a coroutine (starts on this thread,
                // suspends on inner co_awaits, resumes on whichever thread resolves
                // the inner future). onReady() wires the chain to our outer resolver.
                handler->invokeRaw(req, ctx)
                    .onReady([resolve = std::move(resolve)](const RawResult& r) mutable {
                        resolve(r);
                    });
            });
        return future;
    }

    // Sync (or no WorkerPool) — run inline on the calling thread.
    return handler->invokeRaw(request, ctx);
}

std::vector<CapabilityRef> CapabilityBroker::findAll(
    std::string_view capability, int version, const CapabilityQuery& query) const
{
    auto entries = registry_.findHandlers(capability, version);
    std::vector<CapabilityRef> result;
    for (const auto* e : entries) {
        if (!e->enabled) continue;
        auto desc = e->handler->describe(query);
        if (desc.available)
            result.push_back({std::string(e->handler->providerId()),
                              std::string(capability), version});
    }
    return result;
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
    return CapabilityRef{std::string(best->handler->providerId()),
                         std::string(capability), version};
}

} // namespace clickin
