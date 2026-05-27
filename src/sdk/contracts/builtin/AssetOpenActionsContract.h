#pragma once
#include "sdk/contracts/builtin/AssetRef.h"
#include <string>
#include <string_view>
#include <vector>

namespace clickin {

struct AssetAction {
    std::string id;
    std::string label;
};

struct AssetOpenActionsContract {
    static constexpr std::string_view capability = "builtin.asset.open_actions";
    static constexpr int version = 1;

    using Request = AssetRef;

    struct Result {
        std::vector<AssetAction> actions;
    };
};

struct AssetExecuteActionContract {
    static constexpr std::string_view capability = "builtin.asset.execute_action";
    static constexpr int version = 1;

    struct Request {
        AssetRef    assetRef;
        std::string actionId;
    };

    struct Result {
        bool        success = false;
        std::string errorMessage;
    };
};

} // namespace clickin
