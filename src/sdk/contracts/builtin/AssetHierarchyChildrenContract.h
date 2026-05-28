#pragma once
#include "sdk/contracts/builtin/HierarchyNode.h"
#include <string>
#include <string_view>
#include <vector>

namespace clickin {

// Query a plugin for the direct children of a hierarchy node.
// nodeId is the plugin-defined identifier returned by
// AssetHierarchyRootsContract or a previous AssetHierarchyChildrenContract.
struct AssetHierarchyChildrenContract {
    static constexpr std::string_view capability = "builtin.asset.hierarchy.children";
    static constexpr int version = 1;

    struct Request {
        std::string nodeId;   // plugin-defined, opaque to the core
    };

    struct Result {
        std::vector<HierarchyNode> nodes;
    };
};

} // namespace clickin
