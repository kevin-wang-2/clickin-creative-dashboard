#include <gtest/gtest.h>
#include "providers/local_file/LocalFilePlugin.h"
#include "providers/local_audio/LocalAudioPlugin.h"
#include "core/app/CoreContext.h"
#include "core/capability/CapabilityRegistry.h"
#include "core/capability/CapabilityBroker.h"
#include "core/services/DatabaseService.h"
#include "core/services/AssetService.h"
#include "core/services/MetadataService.h"
#include "core/services/CacheService.h"
#include "core/services/JobService.h"
#include "core/services/SettingsService.h"
#include "core/db/Database.h"
#include "sdk/contracts/builtin/AssetDiscoveryContract.h"
#include "sdk/contracts/media/AudioMetadataContract.h"
#include "sdk/contracts/media/AudioWaveformContract.h"
#include "sdk/contracts/media/AudioPreviewContract.h"

#include <filesystem>
#include <fstream>
#include <cstdint>

using namespace clickin;

// ── WAV/AIFF test-file helpers ────────────────────────────────────────────────

static void writeU16LE(std::ofstream& f, uint16_t v) {
    uint8_t b[2] = {uint8_t(v), uint8_t(v >> 8)};
    f.write(reinterpret_cast<const char*>(b), 2);
}
static void writeU32LE(std::ofstream& f, uint32_t v) {
    uint8_t b[4] = {uint8_t(v), uint8_t(v>>8), uint8_t(v>>16), uint8_t(v>>24)};
    f.write(reinterpret_cast<const char*>(b), 4);
}

// Create a minimal 16-bit mono PCM WAV with a square wave so peaks are non-zero.
static void createWavFile(const std::filesystem::path& path,
                           int sampleRate  = 44100,
                           int numFrames   = 44100,
                           int channels    = 1,
                           int bitsPerSample = 16) {
    std::ofstream f(path, std::ios::binary);
    int dataSize = numFrames * channels * (bitsPerSample / 8);
    f.write("RIFF", 4);  writeU32LE(f, uint32_t(36 + dataSize));
    f.write("WAVE", 4);
    f.write("fmt ", 4);  writeU32LE(f, 16);
    writeU16LE(f, 1);                                          // PCM
    writeU16LE(f, uint16_t(channels));
    writeU32LE(f, uint32_t(sampleRate));
    writeU32LE(f, uint32_t(sampleRate * channels * bitsPerSample / 8));
    writeU16LE(f, uint16_t(channels * bitsPerSample / 8));
    writeU16LE(f, uint16_t(bitsPerSample));
    f.write("data", 4);  writeU32LE(f, uint32_t(dataSize));
    // Square wave: alternating +16383 / -16383 per channel per frame.
    for (int i = 0; i < numFrames * channels; ++i) {
        int16_t s = (i % 2 == 0) ? 16383 : -16383;
        uint8_t b[2] = {uint8_t(s), uint8_t(s >> 8)};
        f.write(reinterpret_cast<const char*>(b), 2);
    }
}

// ── Fixture ───────────────────────────────────────────────────────────────────

class LocalAudioPluginTest : public ::testing::Test {
protected:
    void SetUp() override {
        dbPath_ = (std::filesystem::temp_directory_path()
                   / ("clickin_audio_" + Database::generateId() + ".db")).string();
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

        // Create temp folder with a real WAV file (1-second, 44100 Hz, mono 16-bit).
        folder_ = std::filesystem::temp_directory_path()
                  / ("clickin_audio_" + Database::generateId());
        std::filesystem::create_directory(folder_);
        wavPath_ = folder_ / "kick.wav";
        createWavFile(wavPath_, 44100, 44100, 1, 16);
        // A small WAV (1000 frames) for fast waveform tests.
        shortWavPath_ = folder_ / "short.wav";
        createWavFile(shortWavPath_, 44100, 1000, 1, 16);

        coreCtx_ = std::make_unique<CoreContext>(ctx());

        // Initialize local_file plugin first (provides locator + discovery).
        PluginContext lfCtx{"builtin.local_file", *coreCtx_};
        localFile_.initialize(lfCtx);
        for (auto& h : localFile_.createCapabilityHandlers())
            capReg_->registerHandler(std::move(h));

        // Initialize local_audio plugin.
        PluginContext laCtx{"builtin.local_audio", *coreCtx_};
        localAudio_.initialize(laCtx);
        for (auto& h : localAudio_.createCapabilityHandlers())
            capReg_->registerHandler(std::move(h));

        // Discover assets so locator metadata exists.
        auto discRef = broker_->findBest<AssetDiscoveryContract>(CapabilityQuery{});
        broker_->invoke<AssetDiscoveryContract>(
            discRef, AssetDiscoveryContract::Request{"local.folder", folder_.string()}).get();
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

    // Pick any discovered asset ID.
    std::string anyAssetId() {
        auto all = assets_->listAssets();
        return all.empty() ? "" : all[0].id;
    }

    std::string                       dbPath_;
    std::filesystem::path             folder_;
    std::filesystem::path             wavPath_;
    std::filesystem::path             shortWavPath_;
    std::unique_ptr<DatabaseService>  dbSvc_;
    std::unique_ptr<AssetService>     assets_;
    std::unique_ptr<MetadataService>  metadata_;
    std::unique_ptr<CacheService>     cache_;
    std::unique_ptr<JobService>       jobs_;
    std::unique_ptr<SettingsService>  settings_;
    std::unique_ptr<CapabilityRegistry> capReg_;
    std::unique_ptr<CapabilityBroker>   broker_;
    std::unique_ptr<CoreContext>        coreCtx_;
    LocalFilePlugin  localFile_;
    LocalAudioPlugin localAudio_;
};

// ── AudioMetadata ─────────────────────────────────────────────────────────────

TEST_F(LocalAudioPluginTest, MetadataHandlerReturnsDuration) {
    std::string id = anyAssetId();
    ASSERT_FALSE(id.empty());

    auto ref = broker_->findBest<AudioMetadataContract>(CapabilityQuery{});
    ASSERT_TRUE(ref.valid());

    auto m = broker_->invoke<AudioMetadataContract>(ref, AssetRef{id, ""}).get();

    // 44100 frames / 44100 Hz = 1.0 second exactly
    EXPECT_NEAR(m.durationSeconds, 1.0, 0.01);
}

TEST_F(LocalAudioPluginTest, MetadataHandlerReturnsSampleRate) {
    std::string id = anyAssetId();
    ASSERT_FALSE(id.empty());

    auto ref = broker_->findBest<AudioMetadataContract>(CapabilityQuery{});
    auto m   = broker_->invoke<AudioMetadataContract>(ref, AssetRef{id, ""}).get();

    EXPECT_EQ(m.sampleRate, 44100);
    EXPECT_EQ(m.channelCount, 1);
}

TEST_F(LocalAudioPluginTest, MetadataHandlerCacheHit) {
    // Call twice — second call should return same result from MetadataService.
    std::string id = anyAssetId();
    ASSERT_FALSE(id.empty());

    auto ref = broker_->findBest<AudioMetadataContract>(CapabilityQuery{});
    auto m1  = broker_->invoke<AudioMetadataContract>(ref, AssetRef{id, ""}).get();
    auto m2  = broker_->invoke<AudioMetadataContract>(ref, AssetRef{id, ""}).get();

    EXPECT_EQ(m1.sampleRate,      m2.sampleRate);
    EXPECT_EQ(m1.channelCount,    m2.channelCount);
    EXPECT_NEAR(m1.durationSeconds, m2.durationSeconds, 1e-9);
}

// ── AudioWaveform ─────────────────────────────────────────────────────────────

TEST_F(LocalAudioPluginTest, WaveformHandlerReturnsPeaks) {
    std::string id = anyAssetId();
    ASSERT_FALSE(id.empty());

    auto ref = broker_->findBest<AudioWaveformContract>(CapabilityQuery{});
    ASSERT_TRUE(ref.valid());

    auto wf = broker_->invoke<AudioWaveformContract>(ref, AssetRef{id, ""}).get();

    EXPECT_GT(wf.minValues.size(), 0u);
    EXPECT_EQ(wf.minValues.size(), wf.maxValues.size());
    EXPECT_GT(wf.durationSeconds, 0.0);
    EXPECT_GT(wf.framesPerPeak, 0);
}

TEST_F(LocalAudioPluginTest, WaveformPeaksAreNonZeroForSquareWave) {
    std::string id = anyAssetId();
    ASSERT_FALSE(id.empty());

    auto ref = broker_->findBest<AudioWaveformContract>(CapabilityQuery{});
    auto wf  = broker_->invoke<AudioWaveformContract>(ref, AssetRef{id, ""}).get();

    ASSERT_FALSE(wf.maxValues.empty());
    // Square wave: at least some maxValues should be positive.
    float maxPeak = *std::max_element(wf.maxValues.begin(), wf.maxValues.end());
    EXPECT_GT(maxPeak, 0.0f);
}

TEST_F(LocalAudioPluginTest, WaveformHandlerCreatesCacheEntry) {
    std::string id = anyAssetId();
    ASSERT_FALSE(id.empty());

    auto ref = broker_->findBest<AudioWaveformContract>(CapabilityQuery{});
    broker_->invoke<AudioWaveformContract>(ref, AssetRef{id, ""}).get();

    auto entry = cache_->find("builtin.local_audio", ScopeRef{"asset", id},
                               "waveform.peaks", "default", "waveform-peaks-v1");
    ASSERT_TRUE(entry.has_value());
    EXPECT_FALSE(entry->uri.empty());
    EXPECT_GT(entry->sizeBytes, 0);
}

TEST_F(LocalAudioPluginTest, WaveformHandlerCacheHitReturnsConsistentData) {
    std::string id = anyAssetId();
    ASSERT_FALSE(id.empty());

    auto ref = broker_->findBest<AudioWaveformContract>(CapabilityQuery{});
    auto wf1 = broker_->invoke<AudioWaveformContract>(ref, AssetRef{id, ""}).get();
    auto wf2 = broker_->invoke<AudioWaveformContract>(ref, AssetRef{id, ""}).get();

    ASSERT_EQ(wf1.minValues.size(), wf2.minValues.size());
    if (!wf1.minValues.empty()) {
        EXPECT_FLOAT_EQ(wf1.minValues[0], wf2.minValues[0]);
        EXPECT_FLOAT_EQ(wf1.maxValues[0], wf2.maxValues[0]);
    }
}

// ── AudioPreview ──────────────────────────────────────────────────────────────

TEST_F(LocalAudioPluginTest, PreviewHandlerReturnsSessionId) {
    std::string id = anyAssetId();
    ASSERT_FALSE(id.empty());

    auto ref = broker_->findBest<AudioPreviewContract>(CapabilityQuery{});
    ASSERT_TRUE(ref.valid());

    auto pr = broker_->invoke<AudioPreviewContract>(ref, AssetRef{id, ""}).get();
    EXPECT_FALSE(pr.sessionId.empty());
}
