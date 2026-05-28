#pragma once
#include <string>

namespace clickin {

// A node in a plugin's hierarchy graph.
// nodeId is plugin-defined and opaque to the core; the core uses it
// only to query children via AssetHierarchyChildrenContract.
struct HierarchyNode {
    std::string nodeId;      // plugin-defined stable identifier
    std::string assetId;     // corresponding asset table ID
    bool        hasChildren = false;
};

} // namespace clickin
