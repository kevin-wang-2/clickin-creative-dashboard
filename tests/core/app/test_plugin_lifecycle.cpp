#include <gtest/gtest.h>
#include "core/app/PluginManager.h"
#include "core/app/CoreContext.h"
#include "core/services/DatabaseService.h"
#include "core/capability/CapabilityRegistry.h"
#include "core/capability/CapabilityBroker.h"
#include "core/services/AssetService.h"
#include "core/services/MetadataService.h"
#include "core/services/CacheService.h"
#include "core/services/JobService.h"
#include "core/services/SettingsService.h"

#include <filesystem>
#include <stdexcept>

using namespace clickin;

// ── Stub plugins ──────────────────────────────────────────────────────────────

struct GoodPlugin : public IPlugin {
    bool initialized = false;
    bool shutdown_called = false;

    PluginManifest manifest() const override {
        return {"test.good", "Good Plugin", "1.0", true, false};
    }
    void initialize(PluginContext&) override { initialized = true; }
    void shutdown() override { shutdown_called = true; }
    std::vector<std::unique_ptr<IRawCapabilityHandler>> createCapabilityHandlers() override {
        return {};
    }
};

struct ThrowingPlugin : public IPlugin {
    PluginManifest manifest() const override {
        return {"test.throwing", "Throwing Plugin", "1.0", true, false};
    }
    void initialize(PluginContext&) override {
        throw std::runtime_error("intentional init failure");
    }
    void shutdown() override {}
    std::vector<std::unique_ptr<IRawCapabilityHandler>> createCapabilityHandlers() override {
        return {};
    }
};

// ── Fixture ───────────────────────────────────────────────────────────────────

class PluginLifecycleTest : public ::testing::Test {
protected:
    void SetUp() override {
        path_ = (std::filesystem::temp_directory_path()
                 / ("clickin_pltest_" + Database::generateId() + ".db")).string();
        dbSvc_ = std::make_unique<DatabaseService>(path_);
        ASSERT_TRUE(dbSvc_->initialize());

        auto& db = dbSvc_->db();
        assets_   = std::make_unique<AssetService>(db);
        metadata_ = std::make_unique<MetadataService>(db);
        cache_    = std::make_unique<CacheService>(db);
        jobs_     = std::make_unique<JobService>();
        settings_ = std::make_unique<SettingsService>(db);
        capReg_   = std::make_unique<CapabilityRegistry>();
        broker_   = std::make_unique<CapabilityBroker>(*capReg_);
    }

    void TearDown() override {
        broker_.reset(); capReg_.reset();
        settings_.reset(); jobs_.reset(); cache_.reset();
        metadata_.reset(); assets_.reset(); dbSvc_.reset();
        std::filesystem::remove(path_);
    }

    CoreContext ctx() {
        return {*dbSvc_, *assets_, *metadata_, *cache_, *jobs_, *settings_, *broker_};
    }

    std::string path_;
    std::unique_ptr<DatabaseService>    dbSvc_;
    std::unique_ptr<AssetService>       assets_;
    std::unique_ptr<MetadataService>    metadata_;
    std::unique_ptr<CacheService>       cache_;
    std::unique_ptr<JobService>         jobs_;
    std::unique_ptr<SettingsService>    settings_;
    std::unique_ptr<CapabilityRegistry> capReg_;
    std::unique_ptr<CapabilityBroker>   broker_;
};

// ── Tests ─────────────────────────────────────────────────────────────────────

TEST_F(PluginLifecycleTest, SuccessfulPluginIsActive) {
    PluginManager pm(*capReg_);
    auto* raw = new GoodPlugin;
    pm.addPlugin(std::unique_ptr<IPlugin>(raw));

    auto c = ctx();
    pm.activateAll(c);

    auto states = pm.states();
    ASSERT_EQ(states.size(), 1u);
    EXPECT_EQ(states[0].pluginId,   "test.good");
    EXPECT_EQ(states[0].loadStatus, "active");
    EXPECT_TRUE(raw->initialized);
}

TEST_F(PluginLifecycleTest, SuccessfulPluginWritesRegistry) {
    PluginManager pm(*capReg_);
    pm.addPlugin(std::make_unique<GoodPlugin>());

    auto c = ctx();
    pm.activateAll(c);

    auto stmt = dbSvc_->db().prepare(
        "SELECT load_status FROM plugin_registry WHERE plugin_id = 'test.good';"
    );
    ASSERT_TRUE(stmt.has_value());
    ASSERT_TRUE(stmt->step());
    EXPECT_EQ(stmt->columnText(0), "active");
}

TEST_F(PluginLifecycleTest, ThrowingPluginMarkedFailed) {
    PluginManager pm(*capReg_);
    pm.addPlugin(std::make_unique<ThrowingPlugin>());

    auto c = ctx();
    pm.activateAll(c);  // must not throw

    auto states = pm.states();
    ASSERT_EQ(states.size(), 1u);
    EXPECT_EQ(states[0].loadStatus, "failed");
    EXPECT_FALSE(states[0].failReason.empty());
}

TEST_F(PluginLifecycleTest, ThrowingPluginWritesFailedToRegistry) {
    PluginManager pm(*capReg_);
    pm.addPlugin(std::make_unique<ThrowingPlugin>());

    auto c = ctx();
    pm.activateAll(c);

    auto stmt = dbSvc_->db().prepare(
        "SELECT load_status FROM plugin_registry WHERE plugin_id = 'test.throwing';"
    );
    ASSERT_TRUE(stmt.has_value());
    ASSERT_TRUE(stmt->step());
    EXPECT_EQ(stmt->columnText(0), "failed");
}

TEST_F(PluginLifecycleTest, StartupContinuesAfterThrow) {
    // GoodPlugin registered AFTER the throwing one — should still activate.
    PluginManager pm(*capReg_);
    pm.addPlugin(std::make_unique<ThrowingPlugin>());
    auto* good = new GoodPlugin;
    pm.addPlugin(std::unique_ptr<IPlugin>(good));

    auto c = ctx();
    pm.activateAll(c);

    EXPECT_TRUE(good->initialized);
}

TEST_F(PluginLifecycleTest, ShutdownCalledOnActivePlugins) {
    PluginManager pm(*capReg_);
    auto* good = new GoodPlugin;
    pm.addPlugin(std::unique_ptr<IPlugin>(good));

    auto c = ctx();
    pm.activateAll(c);
    pm.shutdownAll();

    EXPECT_TRUE(good->shutdown_called);
}

TEST_F(PluginLifecycleTest, DisabledPluginSkipped) {
    // Pre-insert the plugin as disabled.
    dbSvc_->db().execute(
        "INSERT INTO plugin_registry (plugin_id, enabled, load_status)"
        " VALUES ('test.good', 0, 'disabled');"
    );

    PluginManager pm(*capReg_);
    auto* good = new GoodPlugin;
    pm.addPlugin(std::unique_ptr<IPlugin>(good));

    auto c = ctx();
    pm.activateAll(c);

    EXPECT_FALSE(good->initialized);
    auto states = pm.states();
    EXPECT_EQ(states[0].loadStatus, "disabled");
}
