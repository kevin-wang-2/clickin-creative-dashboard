#include "providers/local_file/LocalFilePlugin.h"
#include "core/app/CoreContext.h"
#include "core/services/AssetService.h"
#include "core/services/HierarchyService.h"
#include "core/services/MetadataService.h"
#include "sdk/TypedCapabilityHandler.h"
#include "sdk/contracts/builtin/AssetDiscoveryContract.h"
#include "sdk/contracts/builtin/AssetLocatorContract.h"
#include "sdk/contracts/builtin/AssetNameContract.h"
#include "sdk/contracts/builtin/AssetKindContract.h"
#include "sdk/contracts/builtin/AssetOpenActionsContract.h"
#include "sdk/contracts/builtin/DiscoverTabContract.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProcess>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <array>
#include <filesystem>
#include <set>
#include <stack>
#include <string>
#include <unordered_map>

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
    DiscoveryHandler(const std::string& pluginId, AssetService& assets,
                     MetadataService& meta, HierarchyService& hier)
        : pluginId_(pluginId), assets_(assets), meta_(meta), hier_(hier) {}

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
        // Normalize: remove trailing separator so filename() is non-empty.
        if (root.filename().empty()) root = root.parent_path();

        std::error_code ec;
        if (!std::filesystem::is_directory(root, ec)) return CapabilityFuture(result);

        // ── Phase 1: collect all audio files under root ───────────────────────
        struct AudioFile { std::filesystem::path path; std::string kind; };
        std::vector<AudioFile> audioFiles;

        for (auto& entry : std::filesystem::recursive_directory_iterator(
                 root, std::filesystem::directory_options::skip_permission_denied, ec)) {
            if (!entry.is_regular_file(ec)) continue;
            if (!isSupportedAudio(entry.path())) continue;
            std::string ext = entry.path().extension().string();
            for (auto& c : ext)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            audioFiles.push_back({entry.path(), extensionToKind(ext)});
        }

        if (audioFiles.empty()) return CapabilityFuture(result);

        // ── Phase 2: find/create root folder asset and clear stale hierarchy ──
        std::string rootUri     = "file://" + root.string();
        std::string rootAssetId = assets_.findAssetByUri(rootUri);
        if (rootAssetId.empty()) {
            rootAssetId = assets_.createAsset(root.filename().string(), "folder");
            assets_.createAssetProvider(rootAssetId, pluginId_, rootUri);
            storeFilePath(meta_, pluginId_, rootAssetId, root.string());
        }

        hier_.removeAllByPlugin(pluginId_, rootAssetId);
        hier_.markNode(pluginId_, rootAssetId);

        // Cache for already-seen directory assets (uri → assetId) to avoid
        // repeated DB lookups when multiple audio files share ancestors.
        std::unordered_map<std::string, std::string> dirCache;
        dirCache[rootUri] = rootAssetId;

        std::string rootStr = root.string();

        // ── Phase 3: build hierarchy along each audio file's ancestor chain ───
        for (auto& [filePath, kind] : audioFiles) {
            // Collect ancestors between root (exclusive) and file's parent (inclusive).
            std::vector<std::filesystem::path> ancestors;
            auto cur = filePath.parent_path();
            while (cur.string() != rootStr && cur != cur.parent_path()) {
                ancestors.push_back(cur);
                cur = cur.parent_path();
            }
            std::reverse(ancestors.begin(), ancestors.end()); // root-child first

            std::string parentId = rootAssetId;
            for (auto& ancestor : ancestors) {
                std::string uri = "file://" + ancestor.string();
                auto it = dirCache.find(uri);
                std::string folderId;
                if (it != dirCache.end()) {
                    folderId = it->second;
                } else {
                    folderId = assets_.findAssetByUri(uri);
                    if (folderId.empty()) {
                        folderId = assets_.createAsset(
                            ancestor.filename().string(), "folder");
                        assets_.createAssetProvider(folderId, pluginId_, uri);
                        storeFilePath(meta_, pluginId_, folderId, ancestor.string());
                    }
                    dirCache[uri] = folderId;
                    hier_.addParent(pluginId_, parentId, folderId);
                }
                parentId = folderId;
            }

            // Create/find the audio file asset and link under its immediate parent.
            std::string fileUri     = "file://" + filePath.string();
            std::string fileAssetId = assets_.findAssetByUri(fileUri);
            if (fileAssetId.empty()) {
                std::string name = filePath.stem().string();
                int64_t size = static_cast<int64_t>(
                    std::filesystem::file_size(filePath, ec));
                fileAssetId = assets_.createAsset(name, kind);
                assets_.createAssetProvider(fileAssetId, pluginId_, fileUri);
                storeFilePath(meta_, pluginId_, fileAssetId, filePath.string());
                result.assets.push_back({name, pluginId_, {}, size});
            }
            hier_.addParent(pluginId_, parentId, fileAssetId);
        }

        return CapabilityFuture(result);
    }

private:
    std::string       pluginId_;
    AssetService&     assets_;
    MetadataService&  meta_;
    HierarchyService& hier_;
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

// builtin.ui.discover.tab:v1
class LocalFileDiscoverTabHandler : public TypedCapabilityHandler<DiscoverTabContract> {
public:
    LocalFileDiscoverTabHandler(const std::string& pluginId,
                                AssetService& assets,
                                MetadataService& meta)
        : pluginId_(pluginId), assets_(assets), meta_(meta) {}

    std::string_view providerId() const override { return pluginId_; }
    CapabilityDescriptor describe(const CapabilityQuery&) override {
        return {.available = true, .priority = 10};
    }

protected:
    CapabilityFuture<DiscoverTabContract::Result>
    invokeTyped(const DiscoverTabContract::Request&, CapabilityContext&) override {
        AssetService*    assets = &assets_;
        MetadataService* meta   = &meta_;
        std::string      pid    = pluginId_;

        return CapabilityFuture(DiscoverTabContract::Result{
            .tabId    = "builtin.local_file.discover",
            .label    = "Local Files",
            .priority = 10,
            .widgetFactory = [assets, meta, pid](QWidget* parent) -> QWidget* {
                return buildScanWidget(assets, meta, pid, parent);
            }
        });
    }

private:
    static QWidget* buildScanWidget(AssetService* assets, MetadataService* meta,
                                    const std::string& pid, QWidget* parent) {
        auto* widget = new QWidget(parent);
        auto* layout = new QVBoxLayout(widget);
        layout->setContentsMargins(12, 12, 12, 12);
        layout->setSpacing(8);

        auto* row       = new QWidget(widget);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);

        auto* pathEdit  = new QLineEdit(row);
        pathEdit->setPlaceholderText("Folder path to scan\xe2\x80\xa6");
        auto* browseBtn = new QPushButton("Browse\xe2\x80\xa6", row);
        auto* scanBtn   = new QPushButton("Scan", row);
        rowLayout->addWidget(pathEdit, 1);
        rowLayout->addWidget(browseBtn);
        rowLayout->addWidget(scanBtn);

        auto* status = new QLabel("Ready.", widget);

        layout->addWidget(row);
        layout->addWidget(status);
        layout->addStretch();

        QObject::connect(browseBtn, &QPushButton::clicked, widget, [pathEdit, widget]() {
            QString dir = QFileDialog::getExistingDirectory(
                widget, "Select folder to scan", QDir::homePath());
            if (!dir.isEmpty()) pathEdit->setText(dir);
        });

        QObject::connect(scanBtn, &QPushButton::clicked, widget,
            [assets, meta, pid, pathEdit, status]() {
                QString qpath = pathEdit->text().trimmed();
                if (qpath.isEmpty()) { status->setText("Enter a folder path first."); return; }

                status->setText("Scanning\xe2\x80\xa6");

                std::filesystem::path root(qpath.toStdString());
                std::error_code ec;
                if (!std::filesystem::is_directory(root, ec)) {
                    status->setText("Not a valid directory.");
                    return;
                }

                int found = 0;
                for (auto& entry : std::filesystem::recursive_directory_iterator(
                         root, std::filesystem::directory_options::skip_permission_denied, ec)) {
                    if (!entry.is_regular_file()) continue;
                    if (!isSupportedAudio(entry.path())) continue;

                    std::string name  = entry.path().stem().string();
                    std::string fpath = entry.path().string();
                    std::string uri   = "file://" + fpath;
                    if (!assets->findAssetByUri(uri).empty()) continue;

                    std::string ext = entry.path().extension().string();
                    for (auto& c : ext)
                        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

                    std::string assetId = assets->createAsset(name, extensionToKind(ext));
                    assets->createAssetProvider(assetId, pid, uri);
                    storeFilePath(*meta, pid, assetId, fpath);
                    ++found;
                }

                status->setText(QString("Done. Found %1 new asset(s).").arg(found));
            });

        return widget;
    }

    std::string      pluginId_;
    AssetService&    assets_;
    MetadataService& meta_;
};

// ── LocalFilePlugin ───────────────────────────────────────────────────────────

PluginManifest LocalFilePlugin::manifest() const {
    return {"builtin.local_file", "Local File", "0.1.0", true, false};
}

void LocalFilePlugin::initialize(PluginContext& ctx) {
    pluginId_  = ctx.pluginId;
    assets_    = &ctx.core.assets;
    metadata_  = &ctx.core.metadata;
    hierarchy_ = &ctx.core.hierarchy;
}

void LocalFilePlugin::shutdown() {}

std::vector<std::unique_ptr<IRawCapabilityHandler>> LocalFilePlugin::createCapabilityHandlers() {
    std::vector<std::unique_ptr<IRawCapabilityHandler>> h;
    h.push_back(std::make_unique<DiscoveryHandler>    (pluginId_, *assets_, *metadata_, *hierarchy_));
    h.push_back(std::make_unique<LocatorHandler>      (pluginId_, *metadata_));
    h.push_back(std::make_unique<NameHandler>         (pluginId_, *metadata_));
    h.push_back(std::make_unique<KindHandler>         (pluginId_, *metadata_));
    h.push_back(std::make_unique<OpenActionsHandler>         (pluginId_));
    h.push_back(std::make_unique<ExecuteActionHandler>       (pluginId_, *metadata_));
    h.push_back(std::make_unique<LocalFileDiscoverTabHandler>(pluginId_, *assets_, *metadata_));
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
