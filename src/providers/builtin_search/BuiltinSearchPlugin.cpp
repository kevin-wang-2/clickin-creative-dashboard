#include "providers/builtin_search/BuiltinSearchPlugin.h"
#include "core/app/CoreContext.h"
#include "core/services/AssetService.h"
#include "sdk/TypedCapabilityHandler.h"
#include "sdk/contracts/builtin/AssetSearchContract.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace clickin {

// ── SearchHandler ─────────────────────────────────────────────────────────────

class SearchHandler : public TypedCapabilityHandler<AssetSearchContract> {
public:
    SearchHandler(const std::string& pluginId, AssetService& assets)
        : pluginId_(pluginId), assets_(assets) {}

    std::string_view providerId() const override { return pluginId_; }

    CapabilityDescriptor describe(const CapabilityQuery&) override {
        return {.available = true, .priority = 1};  // low priority — better plugins can override
    }

protected:
    CapabilityFuture<AssetSearchContract::Result>
    invokeTyped(const AssetSearchContract::Request& req, CapabilityContext&) override {
        auto lower = [](std::string s) {
            for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        };

        std::string q = lower(req.query);
        auto all = assets_.listAssets();

        AssetSearchContract::Result result;
        for (auto& a : all) {
            if (lower(a.name).find(q) != std::string::npos) {
                result.assets.push_back(std::move(a));
                if (req.limit > 0 && static_cast<int>(result.assets.size()) >= req.limit)
                    break;
            }
        }
        return CapabilityFuture(std::move(result));
    }

private:
    std::string   pluginId_;
    AssetService& assets_;
};

// ── BuiltinSearchPlugin ───────────────────────────────────────────────────────

PluginManifest BuiltinSearchPlugin::manifest() const {
    return {"builtin.search", "Builtin Search", "0.1.0", true, false};
}

void BuiltinSearchPlugin::initialize(PluginContext& ctx) {
    pluginId_ = ctx.pluginId;
    assets_   = &ctx.core.assets;
}

void BuiltinSearchPlugin::shutdown() {}

std::vector<std::unique_ptr<IRawCapabilityHandler>>
BuiltinSearchPlugin::createCapabilityHandlers() {
    std::vector<std::unique_ptr<IRawCapabilityHandler>> h;
    h.push_back(std::make_unique<SearchHandler>(pluginId_, *assets_));
    return h;
}

} // namespace clickin
