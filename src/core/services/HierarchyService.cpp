#include "core/services/HierarchyService.h"
#include "core/db/Database.h"

#include <queue>
#include <unordered_set>

namespace clickin {

HierarchyService::HierarchyService(Database& db) : db_(db) {}

std::string HierarchyService::createNode(const std::string& pluginId,
                                          const std::string& assetId) {
    std::string nodeId = Database::generateId();
    auto stmt = db_.prepare(
        "INSERT INTO hierarchy_nodes (node_id, plugin_id, asset_id) VALUES (?, ?, ?);");
    if (!stmt) return {};
    stmt->bindText(1, nodeId).bindText(2, pluginId).bindText(3, assetId);
    stmt->step();
    return nodeId;
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

std::vector<NodeRecord> HierarchyService::getRootNodes() const {
    auto stmt = db_.prepare(
        "SELECT n.node_id, n.plugin_id, n.asset_id, a.name, a.kind, a.status"
        " FROM hierarchy_nodes n"
        " JOIN asset a ON a.id = n.asset_id"
        " WHERE n.node_id NOT IN (SELECT child_node_id FROM hierarchy_edges)"
        "   AND a.status != 'deleted'"
        " ORDER BY a.kind DESC, a.name;");
    if (!stmt) return {};

    std::vector<NodeRecord> result;
    while (stmt->step()) {
        NodeRecord r;
        r.nodeId   = stmt->columnText(0);
        r.pluginId = stmt->columnText(1);
        r.assetId  = stmt->columnText(2);
        r.name     = stmt->columnText(3);
        r.kind     = stmt->columnText(4);
        r.status   = stmt->columnText(5);
        result.push_back(std::move(r));
    }
    return result;
}

std::vector<NodeRecord> HierarchyService::getChildNodes(const std::string& parentNodeId) const {
    auto stmt = db_.prepare(
        "SELECT n.node_id, n.plugin_id, n.asset_id, a.name, a.kind, a.status"
        " FROM hierarchy_edges e"
        " JOIN hierarchy_nodes n ON n.node_id = e.child_node_id"
        " JOIN asset a ON a.id = n.asset_id"
        " WHERE e.parent_node_id = ?"
        "   AND a.status != 'deleted'"
        " ORDER BY a.kind DESC, a.name;");
    if (!stmt) return {};
    stmt->bindText(1, parentNodeId);

    std::vector<NodeRecord> result;
    while (stmt->step()) {
        NodeRecord r;
        r.nodeId   = stmt->columnText(0);
        r.pluginId = stmt->columnText(1);
        r.assetId  = stmt->columnText(2);
        r.name     = stmt->columnText(3);
        r.kind     = stmt->columnText(4);
        r.status   = stmt->columnText(5);
        result.push_back(std::move(r));
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

void HierarchyService::removeAllByPlugin(const std::string& pluginId) {
    auto s1 = db_.prepare(
        "DELETE FROM hierarchy_edges"
        " WHERE parent_node_id IN (SELECT node_id FROM hierarchy_nodes WHERE plugin_id = ?)"
        "    OR child_node_id  IN (SELECT node_id FROM hierarchy_nodes WHERE plugin_id = ?);");
    if (s1) { s1->bindText(1, pluginId).bindText(2, pluginId); s1->step(); }

    auto s2 = db_.prepare("DELETE FROM hierarchy_nodes WHERE plugin_id = ?;");
    if (s2) { s2->bindText(1, pluginId); s2->step(); }
}

void HierarchyService::removeSubtree(const std::string& nodeId) {
    // BFS to collect all reachable node IDs.
    std::unordered_set<std::string> visited;
    std::queue<std::string> queue;
    queue.push(nodeId);

    while (!queue.empty()) {
        std::string cur = queue.front(); queue.pop();
        if (!visited.insert(cur).second) continue;

        auto stmt = db_.prepare(
            "SELECT child_node_id FROM hierarchy_edges WHERE parent_node_id = ?;");
        if (!stmt) continue;
        stmt->bindText(1, cur);
        while (stmt->step())
            queue.push(stmt->columnText(0));
    }

    for (const auto& id : visited) {
        auto s1 = db_.prepare(
            "DELETE FROM hierarchy_edges"
            " WHERE parent_node_id = ? OR child_node_id = ?;");
        if (s1) { s1->bindText(1, id).bindText(2, id); s1->step(); }

        auto s2 = db_.prepare("DELETE FROM hierarchy_nodes WHERE node_id = ?;");
        if (s2) { s2->bindText(1, id); s2->step(); }
    }
}

} // namespace clickin
