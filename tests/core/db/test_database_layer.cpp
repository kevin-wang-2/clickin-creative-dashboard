#include <gtest/gtest.h>
#include "core/db/Database.h"
#include "core/db/Migration.h"
#include "core/services/DatabaseService.h"
#include "core/services/MetadataService.h"
#include "core/services/CacheService.h"
#include "core/services/AssetService.h"
#include "core/services/SettingsService.h"

#include <filesystem>
#include <string>

using namespace clickin;

// ── Test fixture: temporary DB file, cleaned up after each test ───────────────

class DbTest : public ::testing::Test {
protected:
    void SetUp() override {
        path_ = (std::filesystem::temp_directory_path()
                 / ("clickin_test_" + Database::generateId() + ".db")).string();
        svc_  = std::make_unique<DatabaseService>(path_);
        ASSERT_TRUE(svc_->initialize());
    }
    void TearDown() override {
        svc_.reset();
        std::filesystem::remove(path_);
    }

    Database& db() { return svc_->db(); }

    std::string                    path_;
    std::unique_ptr<DatabaseService> svc_;
};

// ── Migration ─────────────────────────────────────────────────────────────────

TEST_F(DbTest, CoreTablesExistAfterInit) {
    // schema_migration must have exactly 3 rows (v1 baseline, v2 uri index, v3 kind column)
    auto stmt = db().prepare("SELECT version FROM schema_migration ORDER BY version;");
    ASSERT_TRUE(stmt.has_value());
    ASSERT_TRUE(stmt->step());
    EXPECT_EQ(stmt->columnInt64(0), 1);
    ASSERT_TRUE(stmt->step());
    EXPECT_EQ(stmt->columnInt64(0), 2);
    ASSERT_TRUE(stmt->step());
    EXPECT_EQ(stmt->columnInt64(0), 3);
    EXPECT_FALSE(stmt->step());
}

TEST_F(DbTest, SecondInitSkipsMigration) {
    // Re-open the same file — runPending should skip already-applied migrations.
    svc_.reset();
    DatabaseService svc2(path_);
    ASSERT_TRUE(svc2.initialize());

    auto stmt = svc2.db().prepare("SELECT count(*) FROM schema_migration;");
    ASSERT_TRUE(stmt.has_value());
    stmt->step();
    EXPECT_EQ(stmt->columnInt64(0), 3);  // still only 3 rows, none re-applied
}

TEST_F(DbTest, PluginMigrationAddedBeforeInit) {
    // A plugin registers its own migration before init.
    DatabaseService svc2(
        (std::filesystem::temp_directory_path()
         / ("clickin_test_plugin_" + Database::generateId() + ".db")).string()
    );
    svc2.addMigration({
        .version     = 100,
        .description = "test plugin table",
        .sql         = "CREATE TABLE plugin_test (id TEXT PRIMARY KEY);"
    });
    ASSERT_TRUE(svc2.initialize());

    auto stmt = svc2.db().prepare(
        "SELECT count(*) FROM schema_migration WHERE version = 100;"
    );
    ASSERT_TRUE(stmt.has_value());
    stmt->step();
    EXPECT_EQ(stmt->columnInt64(0), 1);

    // Cleanup
    std::filesystem::remove(svc2.db().lastError());  // path not exposed, just let TearDown handle
}

// ── MetadataService ───────────────────────────────────────────────────────────

TEST_F(DbTest, MetadataWriteRead) {
    MetadataService meta(db());
    ScopeRef scope{"asset", "asset-001"};
    std::vector<std::byte> data = {std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};

    meta.write("plugin.a", scope, "tags", data, "raw", 1);
    auto rec = meta.read("plugin.a", scope, "tags");

    ASSERT_TRUE(rec.has_value());
    EXPECT_EQ(rec->data, data);
    EXPECT_EQ(rec->dataFormat, "raw");
    EXPECT_EQ(rec->schemaVersion, 1);
}

TEST_F(DbTest, MetadataIsolationByPluginId) {
    MetadataService meta(db());
    ScopeRef scope{"asset", "asset-001"};
    std::vector<std::byte> data = {std::byte{0xAA}};

    meta.write("plugin.a", scope, "ns", data, "raw");

    // plugin.b cannot read plugin.a's record for the same scope+namespace
    auto rec = meta.read("plugin.b", scope, "ns");
    EXPECT_FALSE(rec.has_value());
}

TEST_F(DbTest, MetadataUpsert) {
    MetadataService meta(db());
    ScopeRef scope{"asset", "asset-001"};
    std::vector<std::byte> v1 = {std::byte{0x01}};
    std::vector<std::byte> v2 = {std::byte{0x02}};

    meta.write("plugin.a", scope, "ns", v1, "raw");
    meta.write("plugin.a", scope, "ns", v2, "raw");  // overwrite

    auto rec = meta.read("plugin.a", scope, "ns");
    ASSERT_TRUE(rec.has_value());
    EXPECT_EQ(rec->data, v2);
}

TEST_F(DbTest, MetadataRemove) {
    MetadataService meta(db());
    ScopeRef scope{"asset", "asset-001"};
    std::vector<std::byte> data = {std::byte{0x01}};

    meta.write("plugin.a", scope, "ns", data, "raw");
    meta.remove("plugin.a", scope, "ns");

    EXPECT_FALSE(meta.read("plugin.a", scope, "ns").has_value());
}

// ── CacheService ──────────────────────────────────────────────────────────────

TEST_F(DbTest, CacheRegisterAndFind) {
    CacheService cache(db());
    ScopeRef scope{"asset", "asset-001"};

    auto entry = cache.registerCache("plugin.audio", scope,
                                     "waveform", "sha256:abc", "v1",
                                     "/cache/waveform.bin", 4096);

    EXPECT_FALSE(entry.id.empty());
    EXPECT_EQ(entry.uri, "/cache/waveform.bin");
    EXPECT_EQ(entry.sizeBytes, 4096);
    EXPECT_EQ(entry.status, "ready");

    auto found = cache.find("plugin.audio", scope, "waveform", "sha256:abc", "v1");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->id, entry.id);
    EXPECT_EQ(found->uri, "/cache/waveform.bin");
}

TEST_F(DbTest, CacheMarkAccessed) {
    CacheService cache(db());
    ScopeRef scope{"asset", "asset-001"};

    auto entry = cache.registerCache("plugin.audio", scope,
                                     "waveform", "key", "v1",
                                     "/cache/wf.bin", 1024);
    EXPECT_NO_FATAL_FAILURE(cache.markAccessed(entry.id));
}

TEST_F(DbTest, CacheRemove) {
    CacheService cache(db());
    ScopeRef scope{"asset", "asset-001"};

    auto entry = cache.registerCache("plugin.audio", scope,
                                     "waveform", "key2", "v1",
                                     "/cache/wf2.bin", 512);
    cache.remove(entry.id);

    auto found = cache.find("plugin.audio", scope, "waveform", "key2", "v1");
    EXPECT_FALSE(found.has_value());
}

// ── AssetService ──────────────────────────────────────────────────────────────

TEST_F(DbTest, AssetCreateAndList) {
    AssetService assets(db());

    auto id = assets.createAsset("My Sound");
    EXPECT_FALSE(id.empty());

    auto list = assets.listAssets();
    ASSERT_EQ(list.size(), 1u);
    EXPECT_EQ(list[0].id, id);
    EXPECT_EQ(list[0].name, "My Sound");
    EXPECT_EQ(list[0].status, "active");
}

TEST_F(DbTest, AssetGetById) {
    AssetService assets(db());
    auto id = assets.createAsset("Sample");

    auto rec = assets.getAsset(id);
    EXPECT_EQ(rec.id, id);
    EXPECT_EQ(rec.name, "Sample");
}

TEST_F(DbTest, AssetDeleteHidesFromList) {
    AssetService assets(db());
    auto id = assets.createAsset("ToDelete");

    assets.deleteAsset(id);
    EXPECT_TRUE(assets.listAssets().empty());
}

TEST_F(DbTest, FindAssetByUriReturnsId) {
    AssetService assets(db());
    auto id = assets.createAsset("track");
    assets.createAssetProvider(id, "builtin.local_file", "file:///tmp/track.wav");

    EXPECT_EQ(assets.findAssetByUri("file:///tmp/track.wav"), id);
    EXPECT_TRUE(assets.findAssetByUri("file:///tmp/other.wav").empty());
}

TEST_F(DbTest, FindAssetByUriMissingReturnsEmpty) {
    AssetService assets(db());
    EXPECT_TRUE(assets.findAssetByUri("file:///no/such/file.wav").empty());
}

TEST_F(DbTest, DuplicateProviderUriRejected) {
    AssetService assets(db());
    auto id1 = assets.createAsset("first");
    assets.createAssetProvider(id1, "builtin.local_file", "file:///dup.wav");

    auto id2 = assets.createAsset("second");
    auto r = assets.createAssetProvider(id2, "builtin.local_file", "file:///dup.wav");
    EXPECT_TRUE(r.empty());
    EXPECT_EQ(assets.findAssetByUri("file:///dup.wav"), id1);
}

TEST_F(DbTest, AssetKindStoredOnCreate) {
    AssetService assets(db());
    auto id = assets.createAsset("kick.wav", "audio.wav");

    auto rec = assets.getAsset(id);
    EXPECT_EQ(rec.kind, "audio.wav");
}

TEST_F(DbTest, AssetKindDefaultsEmpty) {
    AssetService assets(db());
    auto id = assets.createAsset("unknown");

    auto rec = assets.getAsset(id);
    EXPECT_TRUE(rec.kind.empty());
}

TEST_F(DbTest, AssetKindSetAfterCreate) {
    AssetService assets(db());
    auto id = assets.createAsset("track");

    assets.setAssetKind(id, "audio.mp3");
    auto rec = assets.getAsset(id);
    EXPECT_EQ(rec.kind, "audio.mp3");
}

TEST_F(DbTest, AssetKindIncludedInList) {
    AssetService assets(db());
    assets.createAsset("snare.flac", "audio.flac");

    auto list = assets.listAssets();
    ASSERT_EQ(list.size(), 1u);
    EXPECT_EQ(list[0].kind, "audio.flac");
}

// ── SettingsService ───────────────────────────────────────────────────────────

TEST_F(DbTest, SettingsGetDefault) {
    SettingsService settings(db());
    EXPECT_EQ(settings.get("missing.key", "default"), "default");
}

TEST_F(DbTest, SettingsSetAndGet) {
    SettingsService settings(db());
    settings.set("app.theme", "dark");
    EXPECT_EQ(settings.get("app.theme"), "dark");
}

TEST_F(DbTest, SettingsOverwrite) {
    SettingsService settings(db());
    settings.set("key", "v1");
    settings.set("key", "v2");
    EXPECT_EQ(settings.get("key"), "v2");
}
