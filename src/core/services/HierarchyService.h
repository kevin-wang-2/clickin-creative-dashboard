#pragma once

#include <string>
#include <unordered_set>
#include <vector>

namespace clickin {

class CapabilityBroker;
class Database;
class SettingsService;

// A positioned reference to an asset within the hierarchy forest.
// One asset can have multiple nodes (different positions, from different plugins).
struct NodeRecord {
    std::string nodeId;      // core-generated UUID, stable across re-traversals for same plugin_node_id
    std::string pluginId;
    std::string assetId;
    std::string name;
    std::string kind;
    std::string status;
};

class HierarchyService {
public:
    explicit HierarchyService(Database& db);

    // ── Cache read (used by UI) ───────────────────────────────────────────────

    // All root nodes (no incoming edge) across all plugins.
    std::vector<NodeRecord> getRootNodes() const;

    // Direct child nodes of parentNodeId.
    std::vector<NodeRecord> getChildNodes(const std::string& parentNodeId) const;

    // True if nodeId has at least one child.
    bool hasChildNodes(const std::string& nodeId) const;

    // ── Cache write (used by traverse, not by plugins directly) ──────────────

    // Upsert a node by (pluginId, pluginNodeId). Returns the stable UUID.
    // If the node already exists, the UUID is preserved; otherwise a new one is generated.
    std::string upsertNode(const std::string& pluginId,
                            const std::string& pluginNodeId,
                            const std::string& assetId);

    // Mark a node as a declared root (is_root = 1). Root is a node attribute;
    // it is independent of whether the node has incoming edges.
    void setNodeAsRoot(const std::string& nodeId);

    // Clear the is_root flag for all nodes belonging to pluginId.
    // Called at the start of traverse to rebuild root flags from scratch.
    void resetRootsForPlugin(const std::string& pluginId);

    // Add a directed edge between two cache nodes (idempotent).
    void addEdge(const std::string& parentNodeId, const std::string& childNodeId);

    // Remove a specific directed edge.
    void removeEdge(const std::string& parentNodeId, const std::string& childNodeId);

    // Remove all edges involving any of this plugin's nodes.
    void clearEdgesForPlugin(const std::string& pluginId);

    // Delete nodes for this plugin whose pluginNodeId is not in activePluginNodeIds.
    void pruneNodes(const std::string& pluginId,
                    const std::unordered_set<std::string>& activePluginNodeIds);

    // Remove all nodes and edges for pluginId.
    void removeAllByPlugin(const std::string& pluginId);

    // Remove nodeId and all reachable descendants.
    void removeSubtree(const std::string& nodeId);

    // ── Traversal ─────────────────────────────────────────────────────────────

    // BFS-traverse a single plugin's graph via AssetHierarchyRootsContract and
    // AssetHierarchyChildrenContract, updating the cache.
    // pluginId must match the providerId of the registered handlers.
    void traverse(const std::string& pluginId, CapabilityBroker& broker);

    // Check version via AssetHierarchyVersionContract and only traverse if
    // changed. Stores last-seen version in SettingsService.
    // Returns true if a re-traversal was performed.
    bool checkAndTraverse(const std::string& pluginId,
                           CapabilityBroker& broker,
                           SettingsService&  settings);

    // Traverse all plugins that have registered an AssetHierarchyRootsContract
    // handler, checking versions first.
    void traverseAll(CapabilityBroker& broker, SettingsService& settings);

private:
    Database& db_;
};

} // namespace clickin
