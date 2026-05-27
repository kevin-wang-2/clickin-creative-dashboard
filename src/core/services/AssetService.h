#pragma once

#include <string>
#include <vector>

namespace clickin {

class Database;

using AssetId = std::string;

struct AssetRecord {
    AssetId     id;
    std::string name;
    std::string status;   // active | missing | deleted
};

class AssetService {
public:
    explicit AssetService(Database& db);

    std::vector<AssetRecord> listAssets() const;
    AssetRecord              getAsset(const AssetId& id) const;
    AssetId                  createAsset(const std::string& name);
    void                     deleteAsset(const AssetId& id);

private:
    Database& db_;
};

} // namespace clickin
