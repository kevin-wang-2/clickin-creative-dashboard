#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace clickin {

struct AssetDiscoveryContract {
    static constexpr std::string_view capability = "builtin.asset.discovery";
    static constexpr int version = 1;

    struct Request {
        std::string sourceType;  // "local.folder"
        std::string uri;
        std::map<std::string, std::string> options;
    };

    struct DiscoveredAsset {
        std::string suggestedName;
        std::string providerId;
        std::string fingerprint;
        int64_t     sizeBytes = -1;
    };

    struct Result {
        std::vector<DiscoveredAsset> assets;
    };
};

} // namespace clickin
