#pragma once

#include <string>
#include <vector>

namespace clickin {

class Database;

// A positioned reference to an asset within the hierarchy forest.
// One asset can have multiple nodes (different positions, potentially from
// different plugins or different views within the same plugin).
struct NodeRecord {
    std::string nodeId;    // unique position identifier
    std::string pluginId;  // plugin that created this node
    std::string assetId;
    std::string name;
    std::string kind;
    std::string status;
};

class HierarchyService {
public:
    explicit HierarchyService(Database& db);

    // Creates a hierarchy position (node) for assetId under pluginId.
    // Returns the new node_id.
    std::string createNode(const std::string& pluginId, const std::string& assetId);

    // Adds a directed parent→child edge between two nodes (idempotent).
    void addEdge(const std::string& parentNodeId, const std::string& childNodeId);

    // Removes an edge.
    void removeEdge(const std::string& parentNodeId, const std::string& childNodeId);

    // Returns all root nodes (those with no incoming edge) across all plugins.
    std::vector<NodeRecord> getRootNodes() const;

    // Returns direct child nodes of parentNodeId.
    std::vector<NodeRecord> getChildNodes(const std::string& parentNodeId) const;

    // Returns true if nodeId has at least one child node.
    bool hasChildNodes(const std::string& nodeId) const;

    // Removes all nodes and edges contributed by pluginId.
    void removeAllByPlugin(const std::string& pluginId);

    // Removes nodeId and all nodes reachable from it (DFS), regardless of plugin.
    // Useful for subtree cleanup without wiping the whole plugin.
    void removeSubtree(const std::string& nodeId);

private:
    Database& db_;
};

} // namespace clickin
