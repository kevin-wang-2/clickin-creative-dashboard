#pragma once

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include "sdk/IRawCapabilityHandler.h"
#include "core/capability/CapabilityTypes.h"

namespace clickin {

class CapabilityRegistry {
public:
    void registerHandler(std::unique_ptr<IRawCapabilityHandler> handler);

    // Returns all handlers registered for the given capability + version.
    std::vector<IRawCapabilityHandler*>
    findHandlers(std::string_view capability, int version) const;

    IRawCapabilityHandler*
    findHandler(const CapabilityRef& ref) const;

private:
    struct Key {
        std::string capability;
        int version;
        bool operator==(const Key&) const = default;
    };
    struct KeyHash {
        std::size_t operator()(const Key& k) const noexcept;
    };

    std::unordered_map<Key, std::vector<std::unique_ptr<IRawCapabilityHandler>>, KeyHash> handlers_;
};

} // namespace clickin
