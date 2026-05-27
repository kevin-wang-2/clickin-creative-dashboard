#pragma once
#include "sdk/contracts/builtin/AssetRef.h"
#include <string>
#include <string_view>

namespace clickin {

struct AssetNameContract {
    static constexpr std::string_view capability = "builtin.asset.name";
    static constexpr int version = 1;

    using Request = AssetRef;

    struct Result {
        std::string name;
        int         confidence = 0;  // 0–100
    };
};

} // namespace clickin
