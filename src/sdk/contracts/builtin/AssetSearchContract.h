#pragma once
#include "sdk/AssetRecord.h"
#include <string>
#include <string_view>
#include <vector>

namespace clickin {

struct AssetSearchContract {
    static constexpr std::string_view capability = "builtin.asset.search";
    static constexpr int version = 1;

    struct Request {
        std::string query;
        int         limit = 200;  // 0 = unlimited
    };

    struct Result {
        std::vector<AssetRecord> assets;
    };
};

} // namespace clickin
