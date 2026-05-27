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

std::string AssetService::createAssetProvider(const std::string& assetId,
                                               const std::string& providerId,
                                               const std::string& uri) {
    std::string id = Database::generateId();
    auto stmt = db_.prepare(
        "INSERT INTO asset_provider (id, asset_id, provider_id, uri) VALUES (?, ?, ?, ?);"
    );
    if (!stmt) return {};
    stmt->bindText(1, id).bindText(2, assetId).bindText(3, providerId).bindText(4, uri);
    stmt->step();
    return id;
}

std::optional<AssetProviderRecord>
AssetService::getAssetProvider(const std::string& assetId, const std::string& providerId) const {
    auto stmt = db_.prepare(
        "SELECT id, asset_id, provider_id, uri FROM asset_provider"
        " WHERE asset_id = ? AND provider_id = ? LIMIT 1;"
    );
    if (!stmt) return std::nullopt;
    stmt->bindText(1, assetId).bindText(2, providerId);
    if (!stmt->step()) return std::nullopt;

    AssetProviderRecord r;
    r.id         = stmt->columnText(0);
    r.assetId    = stmt->columnText(1);
    r.providerId = stmt->columnText(2);
    r.uri        = stmt->columnText(3);
    return r;
}

std::vector<AssetProviderRecord>
AssetService::listAssetProviders(const std::string& assetId) const {
    auto stmt = db_.prepare(
        "SELECT id, asset_id, provider_id, uri FROM asset_provider WHERE asset_id = ?;"
    );
    if (!stmt) return {};
    stmt->bindText(1, assetId);

    std::vector<AssetProviderRecord> result;
    while (stmt->step()) {
        AssetProviderRecord r;
        r.id         = stmt->columnText(0);
        r.assetId    = stmt->columnText(1);
        r.providerId = stmt->columnText(2);
        r.uri        = stmt->columnText(3);
        result.push_back(std::move(r));
    }
    return result;
}

} // namespace clickin
