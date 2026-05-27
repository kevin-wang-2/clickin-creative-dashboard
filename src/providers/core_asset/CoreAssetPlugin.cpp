#include "providers/core_asset/CoreAssetPlugin.h"
#include "core/app/CoreContext.h"
#include "core/services/AssetService.h"
#include "sdk/TypedCapabilityHandler.h"
#include "sdk/contracts/builtin/AssetNameContract.h"
#include "sdk/contracts/builtin/AssetKindContract.h"
#include "sdk/contracts/builtin/AssetThumbnailContract.h"
#include "sdk/contracts/builtin/AssetOpenActionsContract.h"

namespace clickin {

// All handlers register at priority = 0 so domain plugins (priority ≥ 10) always win.

// builtin.asset.name:v1 — read name from the Core asset table
class FallbackNameHandler : public TypedCapabilityHandler<AssetNameContract> {
public:
    FallbackNameHandler(const std::string& pluginId, AssetService& assets)
        : pluginId_(pluginId), assets_(assets) {}

    std::string_view providerId() const override { return pluginId_; }

    CapabilityDescriptor describe(const CapabilityQuery&) override {
        return {.available = true, .priority = 0};
    }

protected:
    CapabilityFuture<AssetNameContract::Result>
    invokeTyped(const AssetRef& req, CapabilityContext&) override {
        auto rec = assets_.getAsset(req.assetId);
        if (rec.id.empty() || rec.name.empty())
            return CapabilityFuture(AssetNameContract::Result{});
        return CapabilityFuture(AssetNameContract::Result{rec.name, 1});
    }

private:
    std::string   pluginId_;
    AssetService& assets_;
};

// builtin.asset.kind:v1 — generic fallback kind
class FallbackKindHandler : public TypedCapabilityHandler<AssetKindContract> {
public:
    explicit FallbackKindHandler(const std::string& pluginId) : pluginId_(pluginId) {}

    std::string_view providerId() const override { return pluginId_; }

    CapabilityDescriptor describe(const CapabilityQuery&) override {
        return {.available = true, .priority = 0};
    }

protected:
    CapabilityFuture<AssetKindContract::Result>
    invokeTyped(const AssetRef&, CapabilityContext&) override {
        return CapabilityFuture(AssetKindContract::Result{"unknown", 1});
    }

private:
    std::string pluginId_;
};

// builtin.asset.thumbnail:v1 — generic icon fallback
class FallbackThumbnailHandler : public TypedCapabilityHandler<AssetThumbnailContract> {
public:
    explicit FallbackThumbnailHandler(const std::string& pluginId) : pluginId_(pluginId) {}

    std::string_view providerId() const override { return pluginId_; }

    CapabilityDescriptor describe(const CapabilityQuery&) override {
        return {.available = true, .priority = 0};
    }

protected:
    CapabilityFuture<AssetThumbnailDescriptor>
    invokeTyped(const AssetRef&, CapabilityContext&) override {
        AssetThumbnailDescriptor d;
        d.kind    = AssetThumbnailDescriptor::Kind::Icon;
        d.iconKey = "generic_asset";
        return CapabilityFuture(d);
    }

private:
    std::string pluginId_;
};

// builtin.asset.open_actions:v1 — generic inspect action
class FallbackOpenActionsHandler : public TypedCapabilityHandler<AssetOpenActionsContract> {
public:
    explicit FallbackOpenActionsHandler(const std::string& pluginId) : pluginId_(pluginId) {}

    std::string_view providerId() const override { return pluginId_; }

    CapabilityDescriptor describe(const CapabilityQuery&) override {
        return {.available = true, .priority = 0};
    }

protected:
    CapabilityFuture<AssetOpenActionsContract::Result>
    invokeTyped(const AssetRef&, CapabilityContext&) override {
        return CapabilityFuture(AssetOpenActionsContract::Result{
            {{"inspect", "Inspect"}}
        });
    }

private:
    std::string pluginId_;
};

// builtin.asset.execute_action:v1 — handle the fallback "inspect" no-op
class FallbackExecuteActionHandler : public TypedCapabilityHandler<AssetExecuteActionContract> {
public:
    explicit FallbackExecuteActionHandler(const std::string& pluginId) : pluginId_(pluginId) {}

    std::string_view providerId() const override { return pluginId_; }

    CapabilityDescriptor describe(const CapabilityQuery&) override {
        return {.available = true, .priority = 0};
    }

protected:
    CapabilityFuture<AssetExecuteActionContract::Result>
    invokeTyped(const AssetExecuteActionContract::Request& req, CapabilityContext&) override {
        if (req.actionId != "inspect")
            return CapabilityFuture(AssetExecuteActionContract::Result{
                false, "unknown action: " + req.actionId});
        // "inspect" is intentionally a no-op at the plugin level;
        // the UI shell handles the actual panel display.
        return CapabilityFuture(AssetExecuteActionContract::Result{true, {}});
    }

private:
    std::string pluginId_;
};

// ── CoreAssetPlugin ───────────────────────────────────────────────────────────

PluginManifest CoreAssetPlugin::manifest() const {
    return {"builtin.core_asset", "Core Asset", "0.1.0", true, true};
}

void CoreAssetPlugin::initialize(PluginContext& ctx) {
    pluginId_ = ctx.pluginId;
    assets_   = &ctx.core.assets;
}

void CoreAssetPlugin::shutdown() {}

std::vector<std::unique_ptr<IRawCapabilityHandler>>
CoreAssetPlugin::createCapabilityHandlers() {
    std::vector<std::unique_ptr<IRawCapabilityHandler>> h;
    h.push_back(std::make_unique<FallbackNameHandler>         (pluginId_, *assets_));
    h.push_back(std::make_unique<FallbackKindHandler>         (pluginId_));
    h.push_back(std::make_unique<FallbackThumbnailHandler>    (pluginId_));
    h.push_back(std::make_unique<FallbackOpenActionsHandler>  (pluginId_));
    h.push_back(std::make_unique<FallbackExecuteActionHandler>(pluginId_));
    return h;
}

} // namespace clickin
