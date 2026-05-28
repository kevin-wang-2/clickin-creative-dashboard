#pragma once
#include <string>
#include <string_view>

namespace clickin {

// Query a plugin for a version token representing the current state of its
// hierarchy graph. The core compares this against the cached version; if
// different (or if the plugin returns an empty string), it re-traverses.
struct AssetHierarchyVersionContract {
    static constexpr std::string_view capability = "builtin.asset.hierarchy.version";
    static constexpr int version = 1;

    struct Request {};

    struct Result {
        // Opaque version token. Empty string means "always consider dirty".
        std::string version;
    };
};

} // namespace clickin
