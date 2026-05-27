#pragma once
#include "core/types/RawPayload.h"
#include <string>
#include <vector>

namespace clickin {

struct AssetRecord {
    AssetId     id;
    std::string name;
    std::string status;   // active, missing, deleted
};

class AssetService {
public:
    // Phase 2 will provide a real implementation backed by DatabaseService.
    std::vector<AssetRecord> listAssets();
    AssetRecord              getAsset(const AssetId& id);
    AssetId                  createAsset(const std::string& name);
    void                     deleteAsset(const AssetId& id);
};

} // namespace clickin
