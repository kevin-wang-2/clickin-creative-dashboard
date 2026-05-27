#include "core/capability/CapabilityRegistry.h"
#include <algorithm>

namespace clickin {

std::size_t CapabilityRegistry::KeyHash::operator()(const Key& k) const noexcept {
    std::size_t h = std::hash<std::string>{}(k.capability);
    h ^= std::hash<int>{}(k.version) + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
}

bool CapabilityRegistry::registerHandler(std::unique_ptr<IRawCapabilityHandler> handler) {
    Key key{std::string(handler->capabilityName()), handler->capabilityVersion()};

    auto& bucket = entries_[key];

    // Reject duplicate providerId for the same capability+version.
    const auto pid = handler->providerId();
    for (const auto& e : bucket) {
        if (e.handler->providerId() == pid)
            return false;
    }

    bucket.push_back(HandlerEntry{std::move(handler), /*enabled=*/true});
    return true;
}

bool CapabilityRegistry::setEnabled(std::string_view providerId,
                                     std::string_view capability,
                                     int              version,
                                     bool             enabled) {
    Key key{std::string(capability), version};
    auto it = entries_.find(key);
    if (it == entries_.end()) return false;

    for (auto& e : it->second) {
        if (e.handler->providerId() == providerId) {
            e.enabled = enabled;
            return true;
        }
    }
    return false;
}

bool CapabilityRegistry::isEnabled(std::string_view providerId,
                                    std::string_view capability,
                                    int              version) const {
    Key key{std::string(capability), version};
    auto it = entries_.find(key);
    if (it == entries_.end()) return false;

    for (const auto& e : it->second)
        if (e.handler->providerId() == providerId)
            return e.enabled;
    return false;
}

std::vector<const HandlerEntry*>
CapabilityRegistry::findHandlers(std::string_view capability, int version) const {
    Key key{std::string(capability), version};
    auto it = entries_.find(key);
    if (it == entries_.end()) return {};

    std::vector<const HandlerEntry*> result;
    result.reserve(it->second.size());
    for (const auto& e : it->second)
        result.push_back(&e);
    return result;
}

const HandlerEntry* CapabilityRegistry::findEntry(const CapabilityRef& ref) const {
    Key key{ref.capability, ref.version};
    auto it = entries_.find(key);
    if (it == entries_.end()) return nullptr;

    for (const auto& e : it->second)
        if (e.handler->providerId() == ref.providerId)
            return &e;
    return nullptr;
}

std::vector<HandlerInfo> CapabilityRegistry::listRegistered() const {
    std::vector<HandlerInfo> out;
    for (const auto& [key, bucket] : entries_) {
        for (const auto& e : bucket) {
            out.push_back(HandlerInfo{
                e.handler->providerId(),
                key.capability,
                key.version,
                e.enabled
            });
        }
    }
    return out;
}

} // namespace clickin
