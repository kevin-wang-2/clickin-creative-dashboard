#pragma once

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include "sdk/IRawCapabilityHandler.h"
#include "core/capability/CapabilityTypes.h"

namespace clickin {

// Registered handler entry — Core owns lifetime and enabled state.
struct HandlerEntry {
    std::unique_ptr<IRawCapabilityHandler> handler;
    bool enabled = true;
};

// Lightweight view returned by listRegistered() for introspection.
struct HandlerInfo {
    std::string_view providerId;
    std::string_view capability;
    int              version  = 0;
    bool             enabled  = true;
};

class CapabilityRegistry {
public:
    // Register a handler. Duplicate (capability, version, providerId) is rejected
    // and returns false; the handler is NOT transferred in that case.
    bool registerHandler(std::unique_ptr<IRawCapabilityHandler> handler);

    // Enable or disable a handler. Returns false if the entry is not found.
    bool setEnabled(std::string_view providerId,
                    std::string_view capability,
                    int              version,
                    bool             enabled);

    bool isEnabled(std::string_view providerId,
                   std::string_view capability,
                   int              version) const;

    // Returns pointers to all handlers for (capability, version), including disabled ones.
    // Callers (Broker) decide whether to honour the enabled state.
    std::vector<const HandlerEntry*>
    findHandlers(std::string_view capability, int version) const;

    // Find the specific handler matching the CapabilityRef (regardless of enabled state).
    const HandlerEntry* findEntry(const CapabilityRef& ref) const;

    // Flat list of all registered handlers for introspection (plugin management UI).
    std::vector<HandlerInfo> listRegistered() const;

private:
    struct Key {
        std::string capability;
        int version;
        bool operator==(const Key&) const = default;
    };
    struct KeyHash {
        std::size_t operator()(const Key& k) const noexcept;
    };

    std::unordered_map<Key, std::vector<HandlerEntry>, KeyHash> entries_;
};

} // namespace clickin
