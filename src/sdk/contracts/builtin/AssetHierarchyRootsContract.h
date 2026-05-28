#pragma once
#include "sdk/contracts/builtin/HierarchyNode.h"
#include <string_view>
#include <vector>

namespace clickin {

// Query a plugin for the root nodes of its hierarchy graph.
// The core calls findAll<AssetHierarchyRootsContract> and BFS-traverses
// each plugin's graph to build the cached hierarchy_nodes/edges tables.
struct AssetHierarchyRootsContract {
    static constexpr std::string_view capability = "builtin.asset.hierarchy.roots";
    static constexpr int version = 1;

    struct Request {};

    struct Result {
        std::vector<HierarchyNode> nodes;
    };
};

} // namespace clickin
