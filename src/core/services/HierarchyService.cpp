#include "core/services/HierarchyService.h"
#include "core/capability/CapabilityBroker.h"
#include "core/db/Database.h"
#include "core/services/SettingsService.h"
#include "sdk/contracts/builtin/AssetHierarchyRootsContract.h"
#include "sdk/contracts/builtin/AssetHierarchyChildrenContract.h"
#include "sdk/contracts/builtin/AssetHierarchyVersionContract.h"

#include <queue>
#include <unordered_set>

namespace clickin {

HierarchyService::HierarchyService(Database& db) : db_(db) {}

// ── Cache read ────────────────────────────────────────────────────────────────

std::vector<NodeRecord> HierarchyService::getRootNodes() const {
    auto stmt = db_.prepare(
        "SELECT n.node_id, n.plugin_id, n.asset_id, a.name, a.kind, a.status"
        " FROM hierarchy_nodes n"
        " JOIN asset a ON a.id = n.asset_id"
        " WHERE n.is_root = 1"
        "   AND a.status != 'deleted'"
        " ORDER BY a.kind DESC, a.name;");
    if (!stmt) return {};
    std::vector<NodeRecord> result;
    while (stmt->step()) {
        result.push_back({stmt->columnText(0), stmt->columnText(1),
                          stmt->columnText(2), stmt->columnText(3),
                          stmt->columnText(4), stmt->columnText(5)});
    }
    return result;
}

std::vector<NodeRecord> HierarchyService::getChildNodes(const std::string& parentNodeId) const {
    auto stmt = db_.prepare(
        "SELECT n.node_id, n.plugin_id, n.asset_id, a.name, a.kind, a.status"
        " FROM hierarchy_edges e"
        " JOIN hierarchy_nodes n ON n.node_id = e.child_node_id"
        " JOIN asset a ON a.id = n.asset_id"
        " WHERE e.parent_node_id = ? AND a.status != 'deleted'"
        " ORDER BY a.kind DESC, a.name;");
    if (!stmt) return {};
    stmt->bindText(1, parentNodeId);
    std::vector<NodeRecord> result;
    while (stmt->step()) {
        result.push_back({stmt->columnText(0), stmt->columnText(1),
                          stmt->columnText(2), stmt->columnText(3),
                          stmt->columnText(4), stmt->columnText(5)});
    }
    return result;
}

bool HierarchyService::hasChildNodes(const std::string& nodeId) const {
    auto stmt = db_.prepare(
        "SELECT 1 FROM hierarchy_edges WHERE parent_node_id = ? LIMIT 1;");
    if (!stmt) return false;
    stmt->bindText(1, nodeId);
    return stmt->step();
}

// ── Cache write ───────────────────────────────────────────────────────────────

std::string HierarchyService::upsertNode(const std::string& pluginId,
                                          const std::string& pluginNodeId,
                                          const std::string& assetId) {
    // Look up existing UUID to preserve it.
    auto sel = db_.prepare(
        "SELECT node_id FROM hierarchy_nodes WHERE plugin_id = ? AND plugin_node_id = ?;");
    if (sel) {
        sel->bindText(1, pluginId).bindText(2, pluginNodeId);
        if (sel->step()) return sel->columnText(0);
    }

    std::string nodeId = Database::generateId();
    auto ins = db_.prepare(
        "INSERT INTO hierarchy_nodes (node_id, plugin_id, plugin_node_id, asset_id)"
        " VALUES (?, ?, ?, ?);");
    if (ins) { ins->bindText(1, nodeId).bindText(2, pluginId)
                    .bindText(3, pluginNodeId).bindText(4, assetId); ins->step(); }
    return nodeId;
}

void HierarchyService::setNodeAsRoot(const std::string& nodeId) {
    auto stmt = db_.prepare(
        "UPDATE hierarchy_nodes SET is_root = 1 WHERE node_id = ?;");
    if (!stmt) return;
    stmt->bindText(1, nodeId);
    stmt->step();
}

void HierarchyService::resetRootsForPlugin(const std::string& pluginId) {
    auto stmt = db_.prepare(
        "UPDATE hierarchy_nodes SET is_root = 0 WHERE plugin_id = ?;");
    if (!stmt) return;
    stmt->bindText(1, pluginId);
    stmt->step();
}

void HierarchyService::addEdge(const std::string& parentNodeId,
                                const std::string& childNodeId) {
    auto stmt = db_.prepare(
        "INSERT OR IGNORE INTO hierarchy_edges (parent_node_id, child_node_id) VALUES (?, ?);");
    if (!stmt) return;
    stmt->bindText(1, parentNodeId).bindText(2, childNodeId);
    stmt->step();
}

void HierarchyService::removeEdge(const std::string& parentNodeId,
                                   const std::string& childNodeId) {
    auto stmt = db_.prepare(
        "DELETE FROM hierarchy_edges WHERE parent_node_id = ? AND child_node_id = ?;");
    if (!stmt) return;
    stmt->bindText(1, parentNodeId).bindText(2, childNodeId);
    stmt->step();
}

void HierarchyService::clearEdgesForPlugin(const std::string& pluginId) {
    auto stmt = db_.prepare(
        "DELETE FROM hierarchy_edges"
        " WHERE parent_node_id IN (SELECT node_id FROM hierarchy_nodes WHERE plugin_id = ?)"
        "    OR child_node_id  IN (SELECT node_id FROM hierarchy_nodes WHERE plugin_id = ?);");
    if (!stmt) return;
    stmt->bindText(1, pluginId).bindText(2, pluginId);
    stmt->step();
}

void HierarchyService::pruneNodes(const std::string& pluginId,
                                   const std::unordered_set<std::string>& activePluginNodeIds) {
    auto sel = db_.prepare(
        "SELECT node_id, plugin_node_id FROM hierarchy_nodes WHERE plugin_id = ?;");
    if (!sel) return;
    sel->bindText(1, pluginId);

    std::vector<std::string> toDelete;
    while (sel->step()) {
        if (!activePluginNodeIds.count(sel->columnText(1)))
            toDelete.push_back(sel->columnText(0));
    }

    for (const auto& nodeId : toDelete) {
        auto s1 = db_.prepare(
            "DELETE FROM hierarchy_edges WHERE parent_node_id = ? OR child_node_id = ?;");
        if (s1) { s1->bindText(1, nodeId).bindText(2, nodeId); s1->step(); }
        auto s2 = db_.prepare("DELETE FROM hierarchy_nodes WHERE node_id = ?;");
        if (s2) { s2->bindText(1, nodeId); s2->step(); }
    }
}

void HierarchyService::removeAllByPlugin(const std::string& pluginId) {
    clearEdgesForPlugin(pluginId);
    auto stmt = db_.prepare("DELETE FROM hierarchy_nodes WHERE plugin_id = ?;");
    if (stmt) { stmt->bindText(1, pluginId); stmt->step(); }
}

void HierarchyService::removeSubtree(const std::string& nodeId) {
    std::unordered_set<std::string> visited;
    std::queue<std::string> q;
    q.push(nodeId);
    while (!q.empty()) {
        std::string cur = q.front(); q.pop();
        if (!visited.insert(cur).second) continue;
        auto stmt = db_.prepare(
            "SELECT child_node_id FROM hierarchy_edges WHERE parent_node_id = ?;");
        if (!stmt) continue;
        stmt->bindText(1, cur);
        while (stmt->step()) q.push(stmt->columnText(0));
    }
    for (const auto& id : visited) {
        auto s1 = db_.prepare(
            "DELETE FROM hierarchy_edges WHERE parent_node_id = ? OR child_node_id = ?;");
        if (s1) { s1->bindText(1, id).bindText(2, id); s1->step(); }
        auto s2 = db_.prepare("DELETE FROM hierarchy_nodes WHERE node_id = ?;");
        if (s2) { s2->bindText(1, id); s2->step(); }
    }
}

// ── Traversal ─────────────────────────────────────────────────────────────────

void HierarchyService::traverse(const std::string& pluginId, CapabilityBroker& broker) {
    // Find this plugin's roots and children handlers.
    CapabilityRef rootsRef, childrenRef;
    for (auto& r : broker.findAll<AssetHierarchyRootsContract>())
        if (r.providerId == pluginId) { rootsRef = r; break; }
    for (auto& r : broker.findAll<AssetHierarchyChildrenContract>())
        if (r.providerId == pluginId) { childrenRef = r; break; }

    if (!rootsRef.valid()) { removeAllByPlugin(pluginId); return; }

    clearEdgesForPlugin(pluginId);
    resetRootsForPlugin(pluginId);

    auto rootsResult = broker.invoke<AssetHierarchyRootsContract>(rootsRef, {}).get();

    struct Item {
        std::string parentNodeId;   // empty = declared root
        HierarchyNode node;
        bool isDeclaredRoot;
    };

    std::queue<Item> bfsQueue;
    for (const auto& n : rootsResult.nodes)
        bfsQueue.push({"", n, true});

    std::unordered_set<std::string> visited;  // plugin_node_id expansion guard

    while (!bfsQueue.empty()) {
        auto [parentNodeId, hn, isDeclaredRoot] = bfsQueue.front();
        bfsQueue.pop();

        // Always upsert the node and wire up edges/root flags, even if already visited.
        // visited only controls whether we *expand* children a second time.
        std::string nodeId = upsertNode(pluginId, hn.nodeId, hn.assetId);
        if (isDeclaredRoot)       setNodeAsRoot(nodeId);
        if (!parentNodeId.empty()) addEdge(parentNodeId, nodeId);

        if (!visited.insert(hn.nodeId).second) continue;  // already expanded — stop here

        if (hn.hasChildren && childrenRef.valid()) {
            auto children = broker.invoke<AssetHierarchyChildrenContract>(
                childrenRef, {hn.nodeId}).get();
            for (const auto& c : children.nodes)
                bfsQueue.push({nodeId, c, false});
        }
    }

    pruneNodes(pluginId, visited);
}

bool HierarchyService::checkAndTraverse(const std::string& pluginId,
                                         CapabilityBroker& broker,
                                         SettingsService&  settings) {
    CapabilityRef vRef;
    for (auto& r : broker.findAll<AssetHierarchyVersionContract>())
        if (r.providerId == pluginId) { vRef = r; break; }

    std::string newVersion;
    if (vRef.valid())
        newVersion = broker.invoke<AssetHierarchyVersionContract>(vRef, {}).get().version;

    const std::string settingsKey = "hierarchy.version." + pluginId;
    if (!newVersion.empty() && settings.get(settingsKey, "") == newVersion)
        return false;

    traverse(pluginId, broker);
    settings.set(settingsKey, newVersion);
    return true;
}

void HierarchyService::traverseAll(CapabilityBroker& broker, SettingsService& settings) {
    std::unordered_set<std::string> seen;
    for (auto& ref : broker.findAll<AssetHierarchyRootsContract>()) {
        if (seen.insert(std::string(ref.providerId)).second)
            checkAndTraverse(std::string(ref.providerId), broker, settings);
    }
}

} // namespace clickin
