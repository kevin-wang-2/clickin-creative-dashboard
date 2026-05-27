#include "core/services/AssetService.h"
#include "core/db/Database.h"

namespace clickin {

AssetService::AssetService(Database& db) : db_(db) {}

std::vector<AssetRecord> AssetService::listAssets() const {
    auto stmt = db_.prepare(
        "SELECT id, name, status FROM asset WHERE status != 'deleted' ORDER BY name;"
    );
    if (!stmt) return {};

    std::vector<AssetRecord> result;
    while (stmt->step()) {
        AssetRecord r;
        r.id     = stmt->columnText(0);
        r.name   = stmt->columnText(1);
        r.status = stmt->columnText(2);
        result.push_back(std::move(r));
    }
    return result;
}

AssetRecord AssetService::getAsset(const AssetId& id) const {
    auto stmt = db_.prepare(
        "SELECT id, name, status FROM asset WHERE id = ? LIMIT 1;"
    );
    if (!stmt) return {};
    stmt->bindText(1, id);

    if (!stmt->step()) return {};
    AssetRecord r;
    r.id     = stmt->columnText(0);
    r.name   = stmt->columnText(1);
    r.status = stmt->columnText(2);
    return r;
}

AssetId AssetService::createAsset(const std::string& name) {
    std::string id = Database::generateId();
    auto stmt = db_.prepare(
        "INSERT INTO asset (id, name) VALUES (?, ?);"
    );
    if (!stmt) return {};
    stmt->bindText(1, id).bindText(2, name);
    stmt->step();
    return id;
}

void AssetService::deleteAsset(const AssetId& id) {
    auto stmt = db_.prepare(
        "UPDATE asset SET status = 'deleted', updated_at = datetime('now') WHERE id = ?;"
    );
    if (!stmt) return;
    stmt->bindText(1, id);
    stmt->step();
}

} // namespace clickin
