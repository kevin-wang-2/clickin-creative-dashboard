#include <gtest/gtest.h>
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
#include "sdk/contracts/builtin/AssetDiscoveryContract.h"
#include "sdk/contracts/builtin/AssetLocatorContract.h"
#include "sdk/contracts/builtin/AssetNameContract.h"
#include "sdk/contracts/builtin/AssetKindContract.h"

#include <filesystem>
#include <fstream>

using namespace clickin;

// ── Fixture ───────────────────────────────────────────────────────────────────

class LocalFilePluginTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Temp DB
        dbPath_ = (std::filesystem::temp_directory_path()
                   / ("clickin_lftest_" + Database::generateId() + ".db")).string();
        dbSvc_ = std::make_unique<DatabaseService>(dbPath_);
        ASSERT_TRUE(dbSvc_->initialize());

        auto& db = dbSvc_->db();
        assets_   = std::make_unique<AssetService>(db);
        metadata_ = std::make_unique<MetadataService>(db);
        cache_    = std::make_unique<CacheService>(db);
        jobs_     = std::make_unique<JobService>();
        settings_ = std::make_unique<SettingsService>(db);
        capReg_   = std::make_unique<CapabilityRegistry>();
        broker_   = std::make_unique<CapabilityBroker>(*capReg_);

        // Temp folder with test audio files
        folder_ = std::filesystem::temp_directory_path()
                  / ("clickin_lf_" + Database::generateId());
        std::filesystem::create_directory(folder_);
        createFile(folder_ / "kick.wav");
        createFile(folder_ / "snare.aiff");
        createFile(folder_ / "bass.flac");
        createFile(folder_ / "notes.txt");  // should be ignored
        std::filesystem::create_directory(folder_ / "sub");
        createFile(folder_ / "sub" / "hi-hat.wav");

        // Initialize plugin
        coreCtx_ = std::make_unique<CoreContext>(ctx());
        PluginContext pctx{"builtin.local_file", *coreCtx_};
        plugin_.initialize(pctx);
        for (auto& h : plugin_.createCapabilityHandlers())
            capReg_->registerHandler(std::move(h));
    }

    void TearDown() override {
        broker_.reset(); capReg_.reset();
        settings_.reset(); jobs_.reset(); cache_.reset();
        metadata_.reset(); assets_.reset(); dbSvc_.reset();
        std::filesystem::remove_all(folder_);
        std::filesystem::remove(dbPath_);
    }

    CoreContext ctx() {
        return {*dbSvc_, *assets_, *metadata_, *cache_, *jobs_, *settings_, *broker_};
    }

    static void createFile(const std::filesystem::path& p) {
        std::ofstream f(p);
        f << "placeholder";
    }

    std::string                       dbPath_;
    std::filesystem::path             folder_;
    std::unique_ptr<DatabaseService>  dbSvc_;
    std::unique_ptr<AssetService>     assets_;
    std::unique_ptr<MetadataService>  metadata_;
    std::unique_ptr<CacheService>     cache_;
    std::unique_ptr<JobService>       jobs_;
    std::unique_ptr<SettingsService>  settings_;
    std::unique_ptr<CapabilityRegistry> capReg_;
    std::unique_ptr<CapabilityBroker>   broker_;
    std::unique_ptr<CoreContext>        coreCtx_;
    LocalFilePlugin                   plugin_;
};

// ── Discovery ─────────────────────────────────────────────────────────────────

TEST_F(LocalFilePluginTest, DiscoveryCreatesAssetRecords) {
    auto c = ctx();
    auto ref = broker_->findBest<AssetDiscoveryContract>(CapabilityQuery{});
    ASSERT_TRUE(ref.valid());

    auto result = broker_->invoke<AssetDiscoveryContract>(
        ref, AssetDiscoveryContract::Request{"local.folder", folder_.string()}).get();

    // 4 audio files (kick.wav, snare.aiff, bass.flac, sub/hi-hat.wav); notes.txt ignored
    EXPECT_EQ(result.assets.size(), 4u);

    auto dbAssets = assets_->listAssets();
    EXPECT_EQ(dbAssets.size(), 4u);
}

TEST_F(LocalFilePluginTest, DiscoveryIgnoresNonAudioFiles) {
    auto ref = broker_->findBest<AssetDiscoveryContract>(CapabilityQuery{});
    auto result = broker_->invoke<AssetDiscoveryContract>(
        ref, AssetDiscoveryContract::Request{"local.folder", folder_.string()}).get();

    for (const auto& a : result.assets)
        EXPECT_NE(a.suggestedName, "notes");
}

TEST_F(LocalFilePluginTest, DiscoveryScansSubdirectories) {
    auto ref = broker_->findBest<AssetDiscoveryContract>(CapabilityQuery{});
    auto result = broker_->invoke<AssetDiscoveryContract>(
        ref, AssetDiscoveryContract::Request{"local.folder", folder_.string()}).get();

    bool foundHiHat = false;
    for (const auto& a : result.assets)
        if (a.suggestedName == "hi-hat") foundHiHat = true;
    EXPECT_TRUE(foundHiHat);
}

TEST_F(LocalFilePluginTest, DiscoveryUnsupportedSourceTypeReturnsEmpty) {
    auto ref = broker_->findBest<AssetDiscoveryContract>(CapabilityQuery{});
    auto result = broker_->invoke<AssetDiscoveryContract>(
        ref, AssetDiscoveryContract::Request{"r2.bucket", "s3://bucket"}).get();

    EXPECT_TRUE(result.assets.empty());
}

// ── Locator ───────────────────────────────────────────────────────────────────

TEST_F(LocalFilePluginTest, LocatorReturnsFileUri) {
    // First run discovery to populate metadata.
    auto discRef = broker_->findBest<AssetDiscoveryContract>(CapabilityQuery{});
    broker_->invoke<AssetDiscoveryContract>(
        discRef, AssetDiscoveryContract::Request{"local.folder", folder_.string()}).get();

    // Pick any asset.
    auto dbAssets = assets_->listAssets();
    ASSERT_FALSE(dbAssets.empty());
    std::string assetId = dbAssets[0].id;

    auto locRef = broker_->findBest<AssetLocatorContract>(CapabilityQuery{});
    ASSERT_TRUE(locRef.valid());

    auto locResult = broker_->invoke<AssetLocatorContract>(
        locRef, AssetRef{assetId, "builtin.local_file"}).get();

    EXPECT_EQ(locResult.scheme, "file");
    EXPECT_TRUE(locResult.uri.starts_with("file://"));
    EXPECT_TRUE(locResult.local);
}

// ── Name ──────────────────────────────────────────────────────────────────────

TEST_F(LocalFilePluginTest, NameHandlerReturnsStem) {
    auto discRef = broker_->findBest<AssetDiscoveryContract>(CapabilityQuery{});
    broker_->invoke<AssetDiscoveryContract>(
        discRef, AssetDiscoveryContract::Request{"local.folder", folder_.string()}).get();

    auto dbAssets = assets_->listAssets();
    ASSERT_FALSE(dbAssets.empty());
    std::string assetId = dbAssets[0].id;

    auto nameRef = broker_->findBest<AssetNameContract>(CapabilityQuery{});
    ASSERT_TRUE(nameRef.valid());
    auto nameResult = broker_->invoke<AssetNameContract>(
        nameRef, AssetRef{assetId, "builtin.local_file"}).get();

    EXPECT_FALSE(nameResult.name.empty());
    EXPECT_GT(nameResult.confidence, 0);
}

// ── Kind ──────────────────────────────────────────────────────────────────────

TEST_F(LocalFilePluginTest, KindHandlerReturnsAudioKind) {
    auto discRef = broker_->findBest<AssetDiscoveryContract>(CapabilityQuery{});
    broker_->invoke<AssetDiscoveryContract>(
        discRef, AssetDiscoveryContract::Request{"local.folder", folder_.string()}).get();

    auto dbAssets = assets_->listAssets();
    ASSERT_FALSE(dbAssets.empty());
    std::string assetId = dbAssets[0].id;

    auto kindRef = broker_->findBest<AssetKindContract>(CapabilityQuery{});
    ASSERT_TRUE(kindRef.valid());
    auto kindResult = broker_->invoke<AssetKindContract>(
        kindRef, AssetRef{assetId, "builtin.local_file"}).get();

    EXPECT_TRUE(kindResult.kind.starts_with("audio."));
    EXPECT_GT(kindResult.confidence, 0);
}
