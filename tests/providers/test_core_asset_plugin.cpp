#include <gtest/gtest.h>
#include "providers/core_asset/CoreAssetPlugin.h"
#include "providers/local_file/LocalFilePlugin.h"
#include "core/app/CoreContext.h"
#include "core/capability/CapabilityRegistry.h"
#include "core/capability/CapabilityBroker.h"
#include "core/services/DatabaseService.h"
#include "core/services/AssetService.h"
#include "core/services/MetadataService.h"
#include "core/services/CacheService.h"
#include "core/services/JobService.h"
#include "core/services/SettingsService.h"
#include "core/services/HierarchyService.h"
#include "core/db/Database.h"
#include "sdk/contracts/builtin/AssetNameContract.h"
#include "sdk/contracts/builtin/AssetKindContract.h"
#include "sdk/contracts/builtin/AssetThumbnailContract.h"
#include "sdk/contracts/builtin/AssetOpenActionsContract.h"

#include <filesystem>
#include <fstream>

using namespace clickin;

// ── Fixture: only core_asset active ──────────────────────────────────────────

class CoreAssetPluginTest : public ::testing::Test {
protected:
    void SetUp() override {
        dbPath_ = (std::filesystem::temp_directory_path()
                   / ("clickin_ca_" + Database::generateId() + ".db")).string();
        dbSvc_ = std::make_unique<DatabaseService>(dbPath_);
        ASSERT_TRUE(dbSvc_->initialize());

        auto& db = dbSvc_->db();
        assets_    = std::make_unique<AssetService>(db);
        metadata_  = std::make_unique<MetadataService>(db);
        cache_     = std::make_unique<CacheService>(db);
        jobs_      = std::make_unique<JobService>();
        settings_  = std::make_unique<SettingsService>(db);
        hierarchy_ = std::make_unique<HierarchyService>(db);
        capReg_    = std::make_unique<CapabilityRegistry>();
        broker_    = std::make_unique<CapabilityBroker>(*capReg_);

        coreCtx_ = std::make_unique<CoreContext>(ctx());

        PluginContext pctx{"builtin.core_asset", *coreCtx_};
        plugin_.initialize(pctx);
        for (auto& h : plugin_.createCapabilityHandlers())
            capReg_->registerHandler(std::move(h));

        // Create an asset directly (no discovery — core_asset must handle anything).
        assetId_ = assets_->createAsset("my_sample");
    }

    void TearDown() override {
        broker_.reset(); capReg_.reset();
        settings_.reset(); hierarchy_.reset(); jobs_.reset(); cache_.reset();
        metadata_.reset(); assets_.reset(); dbSvc_.reset();
        std::filesystem::remove(dbPath_);
    }

    CoreContext ctx() {
        return {*dbSvc_, *assets_, *metadata_, *cache_, *jobs_, *settings_, *hierarchy_, *broker_};
    }

    std::string                       dbPath_;
    std::string                       assetId_;
    std::unique_ptr<DatabaseService>  dbSvc_;
    std::unique_ptr<AssetService>     assets_;
    std::unique_ptr<MetadataService>  metadata_;
    std::unique_ptr<CacheService>     cache_;
    std::unique_ptr<JobService>       jobs_;
    std::unique_ptr<SettingsService>  settings_;
    std::unique_ptr<HierarchyService> hierarchy_;
    std::unique_ptr<CapabilityRegistry> capReg_;
    std::unique_ptr<CapabilityBroker>   broker_;
    std::unique_ptr<CoreContext>        coreCtx_;
    CoreAssetPlugin plugin_;
};

// ── Fallback name ─────────────────────────────────────────────────────────────

TEST_F(CoreAssetPluginTest, FallbackNameReturnsAssetTableName) {
    auto ref = broker_->findBest<AssetNameContract>(CapabilityQuery{});
    ASSERT_TRUE(ref.valid());
    EXPECT_EQ(ref.providerId, "builtin.core_asset");

    auto res = broker_->invoke<AssetNameContract>(ref, AssetRef{assetId_, ""}).get();
    EXPECT_EQ(res.name, "my_sample");
    EXPECT_GT(res.confidence, 0);
}

// ── Fallback kind ─────────────────────────────────────────────────────────────

TEST_F(CoreAssetPluginTest, FallbackKindReturnsUnknown) {
    auto ref = broker_->findBest<AssetKindContract>(CapabilityQuery{});
    ASSERT_TRUE(ref.valid());
    EXPECT_EQ(ref.providerId, "builtin.core_asset");

    auto res = broker_->invoke<AssetKindContract>(ref, AssetRef{assetId_, ""}).get();
    EXPECT_EQ(res.kind, "unknown");
    EXPECT_GT(res.confidence, 0);
}

// ── Fallback thumbnail ────────────────────────────────────────────────────────

TEST_F(CoreAssetPluginTest, FallbackThumbnailReturnsGenericIcon) {
    auto ref = broker_->findBest<AssetThumbnailContract>(CapabilityQuery{});
    ASSERT_TRUE(ref.valid());
    EXPECT_EQ(ref.providerId, "builtin.core_asset");

    auto res = broker_->invoke<AssetThumbnailContract>(ref, AssetRef{assetId_, ""}).get();
    EXPECT_EQ(res.kind, AssetThumbnailDescriptor::Kind::Icon);
    EXPECT_FALSE(res.iconKey.empty());
}

// ── Fallback open_actions ─────────────────────────────────────────────────────

TEST_F(CoreAssetPluginTest, FallbackOpenActionsReturnsInspect) {
    auto ref = broker_->findBest<AssetOpenActionsContract>(CapabilityQuery{});
    ASSERT_TRUE(ref.valid());

    auto res = broker_->invoke<AssetOpenActionsContract>(ref, AssetRef{assetId_, ""}).get();
    ASSERT_FALSE(res.actions.empty());
    EXPECT_EQ(res.actions[0].id, "inspect");
}

// ── Fallback execute_action ───────────────────────────────────────────────────

TEST_F(CoreAssetPluginTest, FallbackExecuteInspectSucceeds) {
    auto ref = broker_->findBest<AssetExecuteActionContract>(CapabilityQuery{});
    ASSERT_TRUE(ref.valid());

    AssetExecuteActionContract::Request req{{assetId_, ""}, "inspect"};
    auto res = broker_->invoke<AssetExecuteActionContract>(ref, req).get();
    EXPECT_TRUE(res.success);
    EXPECT_TRUE(res.errorMessage.empty());
}

TEST_F(CoreAssetPluginTest, FallbackExecuteUnknownActionFails) {
    auto ref = broker_->findBest<AssetExecuteActionContract>(CapabilityQuery{});
    AssetExecuteActionContract::Request req{{assetId_, ""}, "nonexistent_action"};
    auto res = broker_->invoke<AssetExecuteActionContract>(ref, req).get();
    EXPECT_FALSE(res.success);
}

// ── Priority: domain plugin beats fallback ────────────────────────────────────

class CoreAssetPriorityTest : public ::testing::Test {
protected:
    void SetUp() override {
        dbPath_ = (std::filesystem::temp_directory_path()
                   / ("clickin_caprio_" + Database::generateId() + ".db")).string();
        dbSvc_ = std::make_unique<DatabaseService>(dbPath_);
        ASSERT_TRUE(dbSvc_->initialize());

        auto& db = dbSvc_->db();
        assets_    = std::make_unique<AssetService>(db);
        metadata_  = std::make_unique<MetadataService>(db);
        cache_     = std::make_unique<CacheService>(db);
        jobs_      = std::make_unique<JobService>();
        settings_  = std::make_unique<SettingsService>(db);
        hierarchy_ = std::make_unique<HierarchyService>(db);
        capReg_    = std::make_unique<CapabilityRegistry>();
        broker_    = std::make_unique<CapabilityBroker>(*capReg_);

        coreCtx_ = std::make_unique<CoreContext>(ctx());

        // Register core_asset (priority 0) first.
        PluginContext caCtx{"builtin.core_asset", *coreCtx_};
        coreAsset_.initialize(caCtx);
        for (auto& h : coreAsset_.createCapabilityHandlers())
            capReg_->registerHandler(std::move(h));

        // Register local_file (priority 10) second.
        PluginContext lfCtx{"builtin.local_file", *coreCtx_};
        localFile_.initialize(lfCtx);
        for (auto& h : localFile_.createCapabilityHandlers())
            capReg_->registerHandler(std::move(h));
    }

    void TearDown() override {
        broker_.reset(); capReg_.reset();
        settings_.reset(); hierarchy_.reset(); jobs_.reset(); cache_.reset();
        metadata_.reset(); assets_.reset(); dbSvc_.reset();
        std::filesystem::remove(dbPath_);
    }

    CoreContext ctx() {
        return {*dbSvc_, *assets_, *metadata_, *cache_, *jobs_, *settings_, *hierarchy_, *broker_};
    }

    std::string                       dbPath_;
    std::unique_ptr<DatabaseService>  dbSvc_;
    std::unique_ptr<AssetService>     assets_;
    std::unique_ptr<MetadataService>  metadata_;
    std::unique_ptr<CacheService>     cache_;
    std::unique_ptr<JobService>       jobs_;
    std::unique_ptr<SettingsService>  settings_;
    std::unique_ptr<HierarchyService> hierarchy_;
    std::unique_ptr<CapabilityRegistry> capReg_;
    std::unique_ptr<CapabilityBroker>   broker_;
    std::unique_ptr<CoreContext>        coreCtx_;
    CoreAssetPlugin  coreAsset_;
    LocalFilePlugin  localFile_;
};

TEST_F(CoreAssetPriorityTest, LocalFileBeatsCorAssetForName) {
    auto ref = broker_->findBest<AssetNameContract>(CapabilityQuery{});
    ASSERT_TRUE(ref.valid());
    EXPECT_EQ(ref.providerId, "builtin.local_file");
}

TEST_F(CoreAssetPriorityTest, LocalFileBeatsCorAssetForKind) {
    auto ref = broker_->findBest<AssetKindContract>(CapabilityQuery{});
    ASSERT_TRUE(ref.valid());
    EXPECT_EQ(ref.providerId, "builtin.local_file");
}

TEST_F(CoreAssetPriorityTest, CoreAssetWinsForThumbnailWhenNoOtherHandlerRegistered) {
    // No other plugin registers a thumbnail handler at higher priority.
    auto ref = broker_->findBest<AssetThumbnailContract>(CapabilityQuery{});
    ASSERT_TRUE(ref.valid());
    EXPECT_EQ(ref.providerId, "builtin.core_asset");
}
