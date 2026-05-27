#include "core/capability/CapabilityRegistry.h"

namespace clickin {

std::size_t CapabilityRegistry::KeyHash::operator()(const Key& k) const noexcept {
    std::size_t h = std::hash<std::string>{}(k.capability);
    h ^= std::hash<int>{}(k.version) + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
}

void CapabilityRegistry::registerHandler(std::unique_ptr<IRawCapabilityHandler> handler) {
    Key key{std::string(handler->capabilityName()), handler->capabilityVersion()};
    handlers_[key].push_back(std::move(handler));
}

std::vector<IRawCapabilityHandler*>
CapabilityRegistry::findHandlers(std::string_view capability, int version) const {
    Key key{std::string(capability), version};
    auto it = handlers_.find(key);
    if (it == handlers_.end()) return {};

    std::vector<IRawCapabilityHandler*> result;
    result.reserve(it->second.size());
    for (const auto& h : it->second)
        result.push_back(h.get());
    return result;
}

IRawCapabilityHandler*
CapabilityRegistry::findHandler(const CapabilityRef& ref) const {
    auto handlers = findHandlers(ref.capability, ref.version);
    for (auto* h : handlers)
        if (h->providerId() == ref.providerId)
            return h;
    return nullptr;
}

} // namespace clickin
