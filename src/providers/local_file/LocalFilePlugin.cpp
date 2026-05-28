#include "providers/local_file/LocalFilePlugin.h"
#include "core/app/CoreContext.h"
#include "core/services/AssetService.h"
#include "core/services/MetadataService.h"
#include "sdk/TypedCapabilityHandler.h"
#include "sdk/contracts/builtin/AssetDiscoveryContract.h"
#include "sdk/contracts/builtin/AssetLocatorContract.h"
#include "sdk/contracts/builtin/AssetNameContract.h"
#include "sdk/contracts/builtin/AssetKindContract.h"
#include "sdk/contracts/builtin/AssetOpenActionsContract.h"

#include <QDesktopServices>
#include <QDir>
#include <QGroupBox>
#include <QLabel>
#include <QProcess>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <array>
#include <filesystem>
#include <set>
#include <string>

namespace clickin {

// ── Supported audio extensions ────────────────────────────────────────────────

static const std::set<std::string, std::less<>> kAudioExtensions = {
    ".wav", ".wave", ".aif", ".aiff", ".mp3", ".flac", ".ogg",
    ".m4a", ".aac", ".opus", ".wma"
};

static bool isSupportedAudio(const std::filesystem::path& p) {
    auto ext = p.extension().string();
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return kAudioExtensions.count(ext) > 0;
}

// ── Kind helper ───────────────────────────────────────────────────────────────

static std::string extensionToKind(const std::string& ext) {
    if (ext == ".wav" || ext == ".wave") return "audio.wav";
    if (ext == ".aif" || ext == ".aiff") return "audio.aiff";
    if (ext == ".mp3")  return "audio.mp3";
    if (ext == ".flac") return "audio.flac";
    if (ext == ".ogg")  return "audio.ogg";
    if (ext == ".m4a" || ext == ".aac") return "audio.aac";
    if (ext == ".opus") return "audio.opus";
    if (ext == ".wma")  return "audio.wma";
    return "audio";
}

// ── Metadata helpers ──────────────────────────────────────────────────────────

static constexpr std::string_view kNsPath    = "file.path";
static constexpr std::string_view kNsSize    = "file.size_bytes";

static void storeFilePath(MetadataService& meta, const std::string& pluginId,
                           const std::string& assetId, const std::string& path) {
    auto bytes = std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(path.data()), path.size());
    meta.write(pluginId, ScopeRef{"asset", assetId}, kNsPath, bytes, "utf8");
}

static std::string readFilePath(MetadataService& meta, const std::string& pluginId,
                                 const std::string& assetId) {
    auto rec = meta.read(pluginId, ScopeRef{"asset", assetId}, kNsPath);
    if (!rec || rec->data.empty()) return {};
    return {reinterpret_cast<const char*>(rec->data.data()), rec->data.size()};
}

// ── Handlers ──────────────────────────────────────────────────────────────────

// builtin.asset.discovery:v1
class DiscoveryHandler : public TypedCapabilityHandler<AssetDiscoveryContract> {
public:
    DiscoveryHandler(const std::string& pluginId, AssetService& assets, MetadataService& meta)
        : pluginId_(pluginId), assets_(assets), meta_(meta) {}

    std::string_view providerId() const override { return pluginId_; }

    CapabilityDescriptor describe(const CapabilityQuery&) override {
        return {.available = true, .priority = 10};
    }

protected:
    CapabilityFuture<AssetDiscoveryContract::Result>
    invokeTyped(const AssetDiscoveryContract::Request& req, CapabilityContext&) override {
        AssetDiscoveryContract::Result result;

        if (req.sourceType != "local.folder") return CapabilityFuture(result);

        std::filesystem::path root(req.uri);
        std::error_code ec;
        if (!std::filesystem::is_directory(root, ec)) return CapabilityFuture(result);

        for (auto& entry : std::filesystem::recursive_directory_iterator(
                 root, std::filesystem::directory_options::skip_permission_denied, ec)) {
            if (!entry.is_regular_file()) continue;
            if (!isSupportedAudio(entry.path())) continue;

            std::string name = entry.path().stem().string();
            std::string path = entry.path().string();
            int64_t size = static_cast<int64_t>(std::filesystem::file_size(entry.path(), ec));
            std::string uri = "file://" + path;

            // Skip if this URI is already tracked by any provider.
            if (!assets_.findAssetByUri(uri).empty()) continue;

            std::string ext = entry.path().extension().string();
            for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            std::string kind = extensionToKind(ext);

            std::string assetId = assets_.createAsset(name, kind);
            assets_.createAssetProvider(assetId, pluginId_, uri);
            storeFilePath(meta_, pluginId_, assetId, path);

            result.assets.push_back({name, pluginId_, {}, size});
        }

        return CapabilityFuture(result);
    }

private:
    std::string      pluginId_;
    AssetService&    assets_;
    MetadataService& meta_;
};

// builtin.provide_locator:v1
class LocatorHandler : public TypedCapabilityHandler<AssetLocatorContract> {
public:
    LocatorHandler(const std::string& pluginId, MetadataService& meta)
        : pluginId_(pluginId), meta_(meta) {}

    std::string_view providerId() const override { return pluginId_; }

    CapabilityDescriptor describe(const CapabilityQuery&) override {
        return {.available = true, .priority = 10};
    }

protected:
    CapabilityFuture<AssetLocatorContract::Result>
    invokeTyped(const AssetLocatorContract::Request& req, CapabilityContext&) override {
        std::string path = readFilePath(meta_, pluginId_, req.assetId);
        if (path.empty()) return CapabilityFuture(AssetLocatorContract::Result{});

        AssetLocatorContract::Result res;
        res.scheme   = "file";
        res.uri      = "file://" + path;
        res.local    = true;
        res.seekable = true;
        return CapabilityFuture(res);
    }

private:
    std::string      pluginId_;
    MetadataService& meta_;
};

// builtin.asset.name:v1
class NameHandler : public TypedCapabilityHandler<AssetNameContract> {
public:
    NameHandler(const std::string& pluginId, MetadataService& meta)
        : pluginId_(pluginId), meta_(meta) {}

    std::string_view providerId() const override { return pluginId_; }
    CapabilityDescriptor describe(const CapabilityQuery&) override {
        return {.available = true, .priority = 10};
    }

protected:
    CapabilityFuture<AssetNameContract::Result>
    invokeTyped(const AssetNameContract::Request& req, CapabilityContext&) override {
        std::string path = readFilePath(meta_, pluginId_, req.assetId);
        if (path.empty()) return CapabilityFuture(AssetNameContract::Result{});

        std::string stem = std::filesystem::path(path).stem().string();
        return CapabilityFuture(AssetNameContract::Result{stem, 80});
    }

private:
    std::string      pluginId_;
    MetadataService& meta_;
};

// builtin.asset.kind:v1
class KindHandler : public TypedCapabilityHandler<AssetKindContract> {
public:
    KindHandler(const std::string& pluginId, MetadataService& meta)
        : pluginId_(pluginId), meta_(meta) {}

    std::string_view providerId() const override { return pluginId_; }
    CapabilityDescriptor describe(const CapabilityQuery&) override {
        return {.available = true, .priority = 10};
    }

protected:
    CapabilityFuture<AssetKindContract::Result>
    invokeTyped(const AssetKindContract::Request& req, CapabilityContext&) override {
        std::string path = readFilePath(meta_, pluginId_, req.assetId);
        if (path.empty()) return CapabilityFuture(AssetKindContract::Result{});

        std::string ext = std::filesystem::path(path).extension().string();
        for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return CapabilityFuture(AssetKindContract::Result{extensionToKind(ext), 90});
    }

private:
    std::string      pluginId_;
    MetadataService& meta_;
};

// builtin.asset.open_actions:v1
class OpenActionsHandler : public TypedCapabilityHandler<AssetOpenActionsContract> {
public:
    explicit OpenActionsHandler(const std::string& pluginId) : pluginId_(pluginId) {}

    std::string_view providerId() const override { return pluginId_; }
    CapabilityDescriptor describe(const CapabilityQuery&) override {
        return {.available = true, .priority = 10};
    }

protected:
    CapabilityFuture<AssetOpenActionsContract::Result>
    invokeTyped(const AssetOpenActionsContract::Request&, CapabilityContext&) override {
#if defined(__APPLE__)
        AssetAction action{"reveal_in_finder", "Reveal in Finder", AssetAction::Type::Execute};
#elif defined(_WIN32)
        AssetAction action{"show_in_explorer", "Show in Explorer", AssetAction::Type::Execute};
#else
        AssetAction action{"open_folder", "Open Folder", AssetAction::Type::Execute};
#endif
        return CapabilityFuture(AssetOpenActionsContract::Result{{action}});
    }

private:
    std::string pluginId_;
};

// builtin.asset.execute_action:v1
class ExecuteActionHandler : public TypedCapabilityHandler<AssetExecuteActionContract> {
public:
    ExecuteActionHandler(const std::string& pluginId, MetadataService& meta)
        : pluginId_(pluginId), meta_(meta) {}

    std::string_view providerId() const override { return pluginId_; }
    CapabilityDescriptor describe(const CapabilityQuery&) override {
        return {.available = true, .priority = 10};
    }

protected:
    CapabilityFuture<AssetExecuteActionContract::Result>
    invokeTyped(const AssetExecuteActionContract::Request& req, CapabilityContext&) override {
        static constexpr std::array kRevealIds = {
            "reveal_in_finder", "show_in_explorer", "open_folder"
        };
        bool isReveal = std::find(kRevealIds.begin(), kRevealIds.end(), req.actionId)
                        != kRevealIds.end();
        if (!isReveal) {
            return CapabilityFuture(AssetExecuteActionContract::Result{
                false, "unknown action: " + req.actionId});
        }

        std::string path = readFilePath(meta_, pluginId_, req.assetRef.assetId);
        if (path.empty()) {
            return CapabilityFuture(AssetExecuteActionContract::Result{
                false, "asset path not found"});
        }

#if defined(__APPLE__)
        // -R selects the file itself in Finder rather than just opening the folder.
        QProcess::startDetached("open", {"-R", QString::fromStdString(path)});
#elif defined(_WIN32)
        // explorer /select highlights the file in Explorer.
        QProcess::startDetached("explorer",
            {"/select,", QDir::toNativeSeparators(QString::fromStdString(path))});
#else
        QDesktopServices::openUrl(
            QUrl::fromLocalFile(QString::fromStdString(
                std::filesystem::path(path).parent_path().string())));
#endif
        return CapabilityFuture(AssetExecuteActionContract::Result{true, {}});
    }

private:
    std::string      pluginId_;
    MetadataService& meta_;
};

// ── LocalFilePlugin ───────────────────────────────────────────────────────────

PluginManifest LocalFilePlugin::manifest() const {
    return {"builtin.local_file", "Local File", "0.1.0", true, false};
}

void LocalFilePlugin::initialize(PluginContext& ctx) {
    pluginId_ = ctx.pluginId;
    assets_   = &ctx.core.assets;
    metadata_ = &ctx.core.metadata;
}

void LocalFilePlugin::shutdown() {}

std::vector<std::unique_ptr<IRawCapabilityHandler>> LocalFilePlugin::createCapabilityHandlers() {
    std::vector<std::unique_ptr<IRawCapabilityHandler>> h;
    h.push_back(std::make_unique<DiscoveryHandler>    (pluginId_, *assets_, *metadata_));
    h.push_back(std::make_unique<LocatorHandler>      (pluginId_, *metadata_));
    h.push_back(std::make_unique<NameHandler>         (pluginId_, *metadata_));
    h.push_back(std::make_unique<KindHandler>         (pluginId_, *metadata_));
    h.push_back(std::make_unique<OpenActionsHandler>  (pluginId_));
    h.push_back(std::make_unique<ExecuteActionHandler>(pluginId_, *metadata_));
    return h;
}

QWidget* LocalFilePlugin::createPluginWindow(QWidget* parent) {
    auto* win = new QWidget(parent, Qt::Window);
    win->setWindowTitle("Local File Plugin");
    win->setMinimumWidth(340);

    auto* layout = new QVBoxLayout(win);
    layout->setSpacing(8);

    auto addLabel = [&](const QString& text, bool bold = false) {
        auto* lbl = new QLabel(text, win);
        lbl->setWordWrap(true);
        if (bold) {
            QFont f = lbl->font();
            f.setBold(true);
            lbl->setFont(f);
        }
        layout->addWidget(lbl);
    };

    addLabel("Local File Plugin", true);
    addLabel(QString("Version: %1").arg(QString::fromStdString(manifest().version)));
    addLabel(QString("Plugin ID: %1").arg(QString::fromStdString(pluginId_)));

    layout->addSpacing(8);

    auto* capGroup = new QGroupBox("Registered Capabilities", win);
    auto* capLayout = new QVBoxLayout(capGroup);
    for (const char* cap : {
            "builtin.asset.discovery:v1",
            "builtin.provide_locator:v1",
            "builtin.asset.name:v1",
            "builtin.asset.kind:v1",
            "builtin.asset.open_actions:v1",
            "builtin.asset.execute_action:v1"
         }) {
        capLayout->addWidget(new QLabel(QString("• ") + cap, capGroup));
    }
    layout->addWidget(capGroup);
    layout->addStretch();

    return win;
}

} // namespace clickin
