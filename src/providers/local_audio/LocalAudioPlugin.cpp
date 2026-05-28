#include "providers/local_audio/LocalAudioPlugin.h"
#include "providers/local_audio/AudioPreviewWidget.h"
#include "core/app/CoreContext.h"
#include "core/capability/CapabilityBroker.h"
#include "core/services/MetadataService.h"
#include "core/services/CacheService.h"
#include "sdk/TypedCapabilityHandler.h"
#include "providers/audio/contracts/AudioMetadataContract.h"
#include "providers/audio/contracts/AudioWaveformContract.h"
#include "providers/audio/contracts/AudioPreviewContract.h"
#include "sdk/contracts/builtin/AssetLocatorContract.h"
#include "sdk/contracts/ui/AssetPreviewWidgetContract.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace clickin {

// ── Little-endian helpers ─────────────────────────────────────────────────────

static uint16_t u16le(const uint8_t* p) {
    return uint16_t(p[0]) | (uint16_t(p[1]) << 8);
}
static uint32_t u32le(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8)
         | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
static int16_t i16le(const uint8_t* p) { return int16_t(u16le(p)); }

// ── Big-endian helpers (AIFF) ─────────────────────────────────────────────────

static uint16_t u16be(const uint8_t* p) {
    return (uint16_t(p[0]) << 8) | uint16_t(p[1]);
}
static uint32_t u32be(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16)
         | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}
static int16_t i16be(const uint8_t* p) { return int16_t(u16be(p)); }

// 80-bit IEEE 754 extended precision to double
static double ext80ToDouble(const uint8_t* p) {
    int exp = ((p[0] & 0x7F) << 8) | p[1];
    uint64_t mant = 0;
    for (int i = 2; i < 10; ++i) mant = (mant << 8) | p[i];
    if (exp == 0 && mant == 0) return 0.0;
    // value = mant * 2^(exp - 16383 - 63)
    return double(mant) * std::ldexp(1.0, exp - 16383 - 63);
}

// ── WAV header ────────────────────────────────────────────────────────────────

struct WavInfo {
    int     channels      = 0;
    int     sampleRate    = 0;
    int     bitsPerSample = 0;
    int     audioFormat   = 0;  // 1=PCM, 3=IEEE_FLOAT
    int64_t dataOffset    = 0;
    int64_t dataSize      = 0;
};

static std::optional<WavInfo> parseWav(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return std::nullopt;

    uint8_t hdr[12];
    if (!f.read(reinterpret_cast<char*>(hdr), 12)) return std::nullopt;
    if (std::memcmp(hdr, "RIFF", 4) != 0 || std::memcmp(hdr + 8, "WAVE", 4) != 0)
        return std::nullopt;

    WavInfo info;
    bool gotFmt = false, gotData = false;

    while (!gotData) {
        uint8_t ch[8];
        if (!f.read(reinterpret_cast<char*>(ch), 8)) break;
        uint32_t sz = u32le(ch + 4);

        if (std::memcmp(ch, "fmt ", 4) == 0 && sz >= 16) {
            uint8_t fmt[18] = {};
            f.read(reinterpret_cast<char*>(fmt), std::min(sz, 18u));
            if (sz > 18) f.seekg(sz - 18, std::ios::cur);
            info.audioFormat   = u16le(fmt);
            info.channels      = u16le(fmt + 2);
            info.sampleRate    = u32le(fmt + 4);
            info.bitsPerSample = u16le(fmt + 14);
            gotFmt = true;
        } else if (std::memcmp(ch, "data", 4) == 0 && gotFmt) {
            info.dataOffset = static_cast<int64_t>(f.tellg());
            info.dataSize   = sz;
            gotData = true;
        } else {
            f.seekg(sz + (sz & 1), std::ios::cur);
        }
        if (!f) break;
    }

    if (!gotFmt || !gotData
        || info.channels <= 0 || info.sampleRate <= 0 || info.bitsPerSample <= 0
        || (info.audioFormat != 1 && info.audioFormat != 3))
        return std::nullopt;

    return info;
}

// ── AIFF header ───────────────────────────────────────────────────────────────

struct AiffInfo {
    int      channels        = 0;
    double   sampleRate      = 0.0;
    int      bitsPerSample   = 0;
    uint32_t numSampleFrames = 0;
    int64_t  dataOffset      = 0;
};

static std::optional<AiffInfo> parseAiff(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return std::nullopt;

    uint8_t hdr[12];
    if (!f.read(reinterpret_cast<char*>(hdr), 12)) return std::nullopt;
    if (std::memcmp(hdr, "FORM", 4) != 0) return std::nullopt;
    if (std::memcmp(hdr + 8, "AIFF", 4) != 0 && std::memcmp(hdr + 8, "AIFC", 4) != 0)
        return std::nullopt;

    AiffInfo info;
    bool gotComm = false, gotSsnd = false;

    while (!gotSsnd) {
        uint8_t ch[8];
        if (!f.read(reinterpret_cast<char*>(ch), 8)) break;
        uint32_t sz = u32be(ch + 4);

        if (std::memcmp(ch, "COMM", 4) == 0 && sz >= 18) {
            uint8_t comm[26] = {};
            f.read(reinterpret_cast<char*>(comm), std::min(sz, 26u));
            if (sz > 26) f.seekg(sz - 26, std::ios::cur);
            info.channels        = u16be(comm);
            info.numSampleFrames = u32be(comm + 2);
            info.bitsPerSample   = i16be(comm + 6);
            info.sampleRate      = ext80ToDouble(comm + 8);
            gotComm = true;
        } else if (std::memcmp(ch, "SSND", 4) == 0 && gotComm) {
            uint8_t ssnd[8];
            if (!f.read(reinterpret_cast<char*>(ssnd), 8)) break;
            uint32_t offset = u32be(ssnd); // block offset
            if (offset > 0) f.seekg(offset, std::ios::cur);
            info.dataOffset = static_cast<int64_t>(f.tellg());
            gotSsnd = true;
        } else {
            f.seekg(sz + (sz & 1), std::ios::cur);
        }
        if (!f) break;
    }

    if (!gotComm || !gotSsnd
        || info.channels <= 0 || info.sampleRate <= 0.0 || info.bitsPerSample <= 0)
        return std::nullopt;

    return info;
}

// ── Sample reading ────────────────────────────────────────────────────────────

// Read one sample as float [-1, 1]. bigEndian=true for AIFF.
static float readSample(std::ifstream& f, int bits, bool bigEndian) {
    uint8_t b[4];
    if (bits == 16) {
        if (!f.read(reinterpret_cast<char*>(b), 2)) return 0.0f;
        int16_t s = bigEndian ? i16be(b) : i16le(b);
        return s / 32768.0f;
    }
    if (bits == 24) {
        if (!f.read(reinterpret_cast<char*>(b), 3)) return 0.0f;
        int32_t s;
        if (bigEndian)
            s = (int32_t(int8_t(b[0])) << 16) | (int32_t(b[1]) << 8) | b[2];
        else
            s = int32_t(b[0]) | (int32_t(b[1]) << 8) | (int32_t(int8_t(b[2])) << 16);
        return s / 8388608.0f;
    }
    if (bits == 32) {
        if (!f.read(reinterpret_cast<char*>(b), 4)) return 0.0f;
        float v;
        std::memcpy(&v, b, 4);
        return v;
    }
    return 0.0f;
}

// ── Waveform peak computation ─────────────────────────────────────────────────

static constexpr int kTargetPeaks = 1000;

static StandardWaveform computePeaks(std::ifstream& f, int64_t dataOffset,
                                      int64_t totalFrames, int channels,
                                      double sampleRate, int bitsPerSample,
                                      bool bigEndian) {
    if (totalFrames == 0 || channels == 0) return {};
    f.seekg(dataOffset);

    int numPeaks     = int(std::min<int64_t>(totalFrames, kTargetPeaks));
    int framesPerPk  = int(totalFrames / numPeaks);

    StandardWaveform wf;
    wf.durationSeconds = double(totalFrames) / sampleRate;
    wf.channelCount    = 1; // mix-down to mono
    wf.framesPerPeak   = framesPerPk;
    wf.minValues.resize(numPeaks,  1.0f);
    wf.maxValues.resize(numPeaks, -1.0f);

    for (int p = 0; p < numPeaks && f; ++p) {
        float lo =  1.0f, hi = -1.0f;
        for (int i = 0; i < framesPerPk && f; ++i) {
            float sum = 0.0f;
            for (int ch = 0; ch < channels; ++ch)
                sum += readSample(f, bitsPerSample, bigEndian);
            float mixed = sum / channels;
            if (mixed < lo) lo = mixed;
            if (mixed > hi) hi = mixed;
        }
        wf.minValues[p] = lo;
        wf.maxValues[p] = hi;
    }
    return wf;
}

// ── Waveform cache file I/O ───────────────────────────────────────────────────

// Binary format: [uint32 peakCount][double duration][int32 channels]
//                [int32 framesPerPeak][float* min][float* max]

static constexpr std::string_view kCacheType    = "waveform.peaks";
static constexpr std::string_view kCacheKey     = "default";
static constexpr std::string_view kCacheVersion = "waveform-peaks-v1";

static void writePeakFile(const std::filesystem::path& p, const StandardWaveform& wf) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) return;
    auto w32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto w64 = [&](double   v) { f.write(reinterpret_cast<const char*>(&v), 8); };
    auto wI  = [&](int32_t  v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto wF  = [&](float    v) { f.write(reinterpret_cast<const char*>(&v), 4); };

    uint32_t n = uint32_t(wf.minValues.size());
    w32(n);
    w64(wf.durationSeconds);
    wI(int32_t(wf.channelCount));
    wI(int32_t(wf.framesPerPeak));
    for (float v : wf.minValues) wF(v);
    for (float v : wf.maxValues) wF(v);
}

static std::optional<StandardWaveform> readPeakFile(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return std::nullopt;

    auto r32 = [&]() { uint32_t v=0; f.read(reinterpret_cast<char*>(&v), 4); return v; };
    auto r64 = [&]() { double   v=0; f.read(reinterpret_cast<char*>(&v), 8); return v; };
    auto rI  = [&]() { int32_t  v=0; f.read(reinterpret_cast<char*>(&v), 4); return v; };
    auto rF  = [&]() { float    v=0; f.read(reinterpret_cast<char*>(&v), 4); return v; };

    uint32_t n = r32();
    if (!f || n == 0 || n > 100'000) return std::nullopt;

    StandardWaveform wf;
    wf.durationSeconds = r64();
    wf.channelCount    = rI();
    wf.framesPerPeak   = rI();
    if (!f) return std::nullopt;

    wf.minValues.resize(n);
    wf.maxValues.resize(n);
    for (float& v : wf.minValues) v = rF();
    for (float& v : wf.maxValues) v = rF();
    if (!f) return std::nullopt;
    return wf;
}

// ── Metadata storage helpers ──────────────────────────────────────────────────

static constexpr std::string_view kNsAudioMeta = "audio.metadata";

// POD packed struct stored as opaque blob in MetadataService.
// Read back only on the same platform (no cross-platform concern for local cache).
#pragma pack(push, 1)
struct StoredMeta {
    double  duration    = 0.0;
    int32_t sampleRate  = 0;
    int32_t channels    = 0;
    int64_t bitRate     = 0;
    char    codec[16]   = {};
    char    container[16] = {};
};
#pragma pack(pop)

static AudioMetadata loadMeta(MetadataService& meta, const std::string& pid,
                               const std::string& assetId) {
    auto rec = meta.read(pid, ScopeRef{"asset", assetId}, kNsAudioMeta);
    if (!rec || rec->data.size() < sizeof(StoredMeta)) return {};
    StoredMeta s;
    std::memcpy(&s, rec->data.data(), sizeof(s));
    AudioMetadata m;
    m.durationSeconds = s.duration;
    m.sampleRate      = s.sampleRate;
    m.channelCount    = s.channels;
    m.bitRate         = s.bitRate;
    m.codec           = s.codec;
    m.container       = s.container;
    return m;
}

static void saveMeta(MetadataService& meta, const std::string& pid,
                     const std::string& assetId, const AudioMetadata& m) {
    StoredMeta s;
    s.duration   = m.durationSeconds;
    s.sampleRate = m.sampleRate;
    s.channels   = m.channelCount;
    s.bitRate    = m.bitRate;
    std::strncpy(s.codec,     m.codec.c_str(),     sizeof(s.codec) - 1);
    std::strncpy(s.container, m.container.c_str(), sizeof(s.container) - 1);
    auto sp = std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(&s), sizeof(s));
    meta.write(pid, ScopeRef{"asset", assetId}, kNsAudioMeta, sp, "binary");
}

// ── Locator helper ────────────────────────────────────────────────────────────

// Invoke the best available AssetLocatorContract handler and return the local path.
// Returns empty string on failure.
static std::string resolveLocalPath(CapabilityBroker& broker, const AssetRef& ref) {
    auto locRef = broker.findBest<AssetLocatorContract>(CapabilityQuery{});
    if (!locRef.valid()) return {};
    auto res = broker.invoke<AssetLocatorContract>(locRef, ref).get();
    if (!res.local || res.uri.size() < 8) return {}; // "file://" = 7 chars + at least 1
    return res.uri.substr(7); // strip "file://"
}

// ── AudioMetadataHandler ──────────────────────────────────────────────────────

class AudioMetadataHandler : public TypedCapabilityHandler<AudioMetadataContract> {
public:
    AudioMetadataHandler(const std::string& pluginId, MetadataService& meta)
        : pluginId_(pluginId), meta_(meta) {}

    std::string_view providerId() const override { return pluginId_; }

    CapabilityDescriptor describe(const CapabilityQuery&) override {
        return {.available = true, .priority = 10};
    }

protected:
    CapabilityFuture<AudioMetadata>
    invokeTyped(const AssetRef& req, CapabilityContext& ctx) override {
        // Check MetadataService cache first.
        {
            AudioMetadata cached = loadMeta(meta_, pluginId_, req.assetId);
            if (cached.sampleRate > 0) return CapabilityFuture(cached);
        }

        if (!ctx.broker()) return CapabilityFuture(AudioMetadata{});
        std::string path = resolveLocalPath(*ctx.broker(), req);
        if (path.empty()) return CapabilityFuture(AudioMetadata{});

        AudioMetadata m;
        std::string ext = std::filesystem::path(path).extension().string();
        for (auto& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));

        if (ext == ".wav" || ext == ".wave") {
            auto info = parseWav(path);
            if (info) {
                int bpf = info->channels * (info->bitsPerSample / 8);
                int64_t frames = bpf > 0 ? info->dataSize / bpf : 0;
                m.durationSeconds = frames > 0 ? double(frames) / info->sampleRate : 0.0;
                m.sampleRate      = info->sampleRate;
                m.channelCount    = info->channels;
                m.bitRate         = int64_t(info->sampleRate) * info->channels * info->bitsPerSample;
                m.codec           = info->audioFormat == 3 ? "pcm_f32" : "pcm";
                m.container       = "wav";
            }
        } else if (ext == ".aif" || ext == ".aiff") {
            auto info = parseAiff(path);
            if (info) {
                m.durationSeconds = info->sampleRate > 0
                    ? double(info->numSampleFrames) / info->sampleRate : 0.0;
                m.sampleRate      = int(info->sampleRate);
                m.channelCount    = info->channels;
                m.bitRate         = int64_t(info->sampleRate) * info->channels * info->bitsPerSample;
                m.codec           = "pcm";
                m.container       = "aiff";
            }
        }
        // Other formats: return default-constructed AudioMetadata (zero values).

        if (m.sampleRate > 0)
            saveMeta(meta_, pluginId_, req.assetId, m);

        return CapabilityFuture(m);
    }

private:
    std::string      pluginId_;
    MetadataService& meta_;
};

// ── AudioWaveformHandler ──────────────────────────────────────────────────────

class AudioWaveformHandler : public TypedCapabilityHandler<AudioWaveformContract> {
public:
    AudioWaveformHandler(const std::string& pluginId, CacheService& cache)
        : pluginId_(pluginId), cache_(cache) {}

    std::string_view providerId() const override { return pluginId_; }

    CapabilityDescriptor describe(const CapabilityQuery&) override {
        return {.available = true, .priority = 10};
    }

protected:
    CapabilityFuture<StandardWaveform>
    invokeTyped(const AssetRef& req, CapabilityContext& ctx) override {
        ScopeRef scope{"asset", req.assetId};

        // Check CacheService for an existing waveform entry.
        auto entry = cache_.find(pluginId_, scope, kCacheType, kCacheKey, kCacheVersion);
        if (entry) {
            // Strip "file://" from the URI to get the local path.
            std::string cachePath = entry->uri.size() > 7
                ? entry->uri.substr(7) : "";
            auto wf = readPeakFile(cachePath);
            if (wf) return CapabilityFuture(*wf);
            // File gone — fall through to recompute.
        }

        if (!ctx.broker()) return CapabilityFuture(StandardWaveform{});
        std::string path = resolveLocalPath(*ctx.broker(), req);
        if (path.empty()) return CapabilityFuture(StandardWaveform{});

        std::string ext = std::filesystem::path(path).extension().string();
        for (auto& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));

        StandardWaveform wf;

        if (ext == ".wav" || ext == ".wave") {
            auto info = parseWav(path);
            if (info) {
                std::ifstream f(path, std::ios::binary);
                int bpf = info->channels * (info->bitsPerSample / 8);
                int64_t frames = bpf > 0 ? info->dataSize / bpf : 0;
                wf = computePeaks(f, info->dataOffset, frames,
                                  info->channels, info->sampleRate,
                                  info->bitsPerSample, /*bigEndian=*/false);
            }
        } else if (ext == ".aif" || ext == ".aiff") {
            auto info = parseAiff(path);
            if (info) {
                std::ifstream f(path, std::ios::binary);
                wf = computePeaks(f, info->dataOffset, info->numSampleFrames,
                                  info->channels, info->sampleRate,
                                  info->bitsPerSample, /*bigEndian=*/true);
            }
        }

        if (wf.minValues.empty()) return CapabilityFuture(StandardWaveform{});

        // Write peak file to disk and register in CacheService.
        std::filesystem::path cacheDir =
            std::filesystem::temp_directory_path() / "clickin_waveforms";
        std::error_code ec;
        std::filesystem::create_directories(cacheDir, ec);
        std::filesystem::path cacheFile = cacheDir / (req.assetId + ".peaks");

        writePeakFile(cacheFile, wf);

        int64_t fileSize = 0;
        if (std::filesystem::exists(cacheFile, ec))
            fileSize = int64_t(std::filesystem::file_size(cacheFile, ec));

        std::string uri = "file://" + cacheFile.string();
        cache_.registerCache(pluginId_, scope, kCacheType, kCacheKey,
                             kCacheVersion, uri, fileSize);

        return CapabilityFuture(wf);
    }

private:
    std::string   pluginId_;
    CacheService& cache_;
};

// ── AudioPreviewHandler (MVP stub) ────────────────────────────────────────────

class AudioPreviewHandler : public TypedCapabilityHandler<AudioPreviewContract> {
public:
    explicit AudioPreviewHandler(const std::string& pluginId) : pluginId_(pluginId) {}

    std::string_view providerId() const override { return pluginId_; }

    CapabilityDescriptor describe(const CapabilityQuery&) override {
        return {.available = true, .priority = 10};
    }

protected:
    CapabilityFuture<PreviewSessionRef>
    invokeTyped(const AssetRef& req, CapabilityContext&) override {
        // Placeholder session ID — full QMediaPlayer integration in Phase 5.
        PreviewSessionRef ref;
        ref.sessionId    = pluginId_ + ":" + req.assetId;
        ref.supportsSeek  = true;
        ref.supportsPause = true;
        ref.supportsLoop  = false;
        return CapabilityFuture(ref);
    }

private:
    std::string pluginId_;
};

// ── AssetPreviewWidgetHandler ─────────────────────────────────────────────────

class AssetPreviewWidgetHandler
    : public TypedCapabilityHandler<AssetPreviewWidgetContract> {
public:
    AssetPreviewWidgetHandler(const std::string& pluginId, CapabilityBroker& broker)
        : pluginId_(pluginId), broker_(broker) {}

    std::string_view providerId() const override { return pluginId_; }

    CapabilityDescriptor describe(const CapabilityQuery&) override {
        return {.available = true, .priority = 10};
    }

protected:
    CapabilityFuture<AssetPreviewWidgetContract::Result>
    invokeTyped(const AssetRef& req, CapabilityContext&) override {
        std::string assetId = req.assetId;
        CapabilityBroker* brokerPtr = &broker_;

        auto factory = [assetId, brokerPtr](QWidget* parent) -> QWidget* {
            return new AudioPreviewWidget(assetId, brokerPtr, parent);
        };
        AssetPreviewWidgetContract::Result result;
        result.supportsEmbedded = true;
        result.supportsWindow   = true;
        result.embeddedFactory  = factory;
        result.windowFactory    = factory;
        return CapabilityFuture(std::move(result));
    }

private:
    std::string        pluginId_;
    CapabilityBroker&  broker_;
};

// ── LocalAudioPlugin ──────────────────────────────────────────────────────────

PluginManifest LocalAudioPlugin::manifest() const {
    return {"builtin.local_audio", "Local Audio", "0.1.0", true, false};
}

void LocalAudioPlugin::initialize(PluginContext& ctx) {
    pluginId_ = ctx.pluginId;
    metadata_ = &ctx.core.metadata;
    cache_    = &ctx.core.cache;
    broker_   = &ctx.core.capabilities;
}

void LocalAudioPlugin::shutdown() {}

std::vector<std::unique_ptr<IRawCapabilityHandler>>
LocalAudioPlugin::createCapabilityHandlers() {
    std::vector<std::unique_ptr<IRawCapabilityHandler>> h;
    h.push_back(std::make_unique<AudioMetadataHandler>    (pluginId_, *metadata_));
    h.push_back(std::make_unique<AudioWaveformHandler>    (pluginId_, *cache_));
    h.push_back(std::make_unique<AudioPreviewHandler>     (pluginId_));
    h.push_back(std::make_unique<AssetPreviewWidgetHandler>(pluginId_, *broker_));
    return h;
}

} // namespace clickin
