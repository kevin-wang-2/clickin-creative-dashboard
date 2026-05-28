#include "providers/local_file/LocalFilePlugin.h"
#include "core/app/CoreContext.h"
#include "core/db/Database.h"
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
#include "sdk/contracts/builtin/AssetHierarchyRootsContract.h"
#include "sdk/contracts/builtin/AssetHierarchyChildrenContract.h"
#include "sdk/contracts/builtin/AssetHierarchyVersionContract.h"
#include "sdk/contracts/builtin/HierarchyNode.h"
#include "core/capability/CapabilityBroker.h"

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
#include <sstream>
#include <stack>
#include <string>
#include <unordered_map>
#include <unordered_set>

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

static constexpr std::string_view kNsPath          = "file.path";
static constexpr std::string_view kNsHierarchyGraph = "hierarchy";

static void storeFilePath(MetadataService& meta, const std::string& pluginId,
                           const std::string& assetId, const std::string& path) {
    auto bytes = std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(path.data()), path.size());
    meta.write(pluginId, ScopeRef{"plugin.asset", assetId}, kNsPath, bytes, "utf8");
}

static std::string readFilePath(MetadataService& meta, const std::string& pluginId,
                                 const std::string& assetId) {
    auto rec = meta.read(pluginId, ScopeRef{"plugin.asset", assetId}, kNsPath);
    if (!rec || rec->data.empty()) return {};
    return {reinterpret_cast<const char*>(rec->data.data()), rec->data.size()};
}

// ── Hierarchy graph blob ──────────────────────────────────────────────────────
// The entire adjacency list is packed into one MetadataService entry per plugin
// (scope="plugin", ns="hierarchy") so the number of DB rows does not scale with
// the number of nodes. Format is plugin-defined; this plugin uses plain text:
//
//   v:<versionToken>\n
//   r:<rootAssetId>\n          (one line per root)
//   c:<parentId>:<c1>,<c2>\n  (one line per parent that has children)
//
// All IDs are UUIDs ([0-9a-f-]), so ':', ',' and '\n' are safe delimiters.

struct HierarchyGraph {
    std::string version;
    std::vector<std::string>                              roots;
    std::unordered_map<std::string, std::vector<std::string>> children; // parentId → childIds
};

static std::vector<std::byte> serializeGraph(const HierarchyGraph& g) {
    std::string buf;
    buf.reserve(64 + g.roots.size() * 40 + g.children.size() * 80);
    buf += "v:"; buf += g.version; buf += '\n';
    for (const auto& r : g.roots) {
        buf += "r:"; buf += r; buf += '\n';
    }
    for (const auto& [parent, kids] : g.children) {
        buf += "c:"; buf += parent; buf += ':';
        for (size_t i = 0; i < kids.size(); ++i) {
            if (i) buf += ',';
            buf += kids[i];
        }
        buf += '\n';
    }
    return {reinterpret_cast<const std::byte*>(buf.data()),
            reinterpret_cast<const std::byte*>(buf.data() + buf.size())};
}

static HierarchyGraph parseGraph(const std::vector<std::byte>& bytes) {
    HierarchyGraph g;
    if (bytes.empty()) return g;
    std::string s(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    std::istringstream ss(s);
    std::string line;
    while (std::getline(ss, line)) {
        if (line.size() < 2) continue;
        const char tag = line[0];
        if (tag == 'v' && line[1] == ':') {
            g.version = line.substr(2);
        } else if (tag == 'r' && line[1] == ':') {
            g.roots.push_back(line.substr(2));
        } else if (tag == 'c' && line[1] == ':') {
            auto rest   = line.substr(2);
            auto colon  = rest.find(':');
            if (colon == std::string::npos) continue;
            std::string parent   = rest.substr(0, colon);
            std::string childStr = rest.substr(colon + 1);
            std::vector<std::string> kids;
            std::istringstream cs(childStr);
            std::string kid;
            while (std::getline(cs, kid, ','))
                if (!kid.empty()) kids.push_back(kid);
            g.children[std::move(parent)] = std::move(kids);
        }
    }
    return g;
}

static void storeHierarchyGraph(MetadataService& meta, const std::string& pluginId,
                                  const HierarchyGraph& g) {
    auto bytes = serializeGraph(g);
    meta.write(pluginId, ScopeRef{"plugin", pluginId}, kNsHierarchyGraph,
               std::span<const std::byte>(bytes), "text/local-file-hierarchy");
}

static HierarchyGraph loadHierarchyGraph(MetadataService& meta, const std::string& pluginId) {
    auto rec = meta.read(pluginId, ScopeRef{"plugin", pluginId}, kNsHierarchyGraph);
    if (!rec) return {};
    return parseGraph(rec->data);
}

// ── Handlers ──────────────────────────────────────────────────────────────────

// builtin.asset.discovery:v1
class DiscoveryHandler : public TypedCapabilityHandler<AssetDiscoveryContract> {
public:
    DiscoveryHandler(const std::string& pluginId, AssetService& assets,
                     MetadataService& meta, HierarchyService& hier)
        : pluginId_(pluginId), assets_(assets), meta_(meta), hier_(hier) {}

    std::string_view providerId()      const override { return pluginId_; }
    ExecutionPolicy  executionPolicy() const override { return ExecutionPolicy::Async; }

    CapabilityDescriptor describe(const CapabilityQuery&) override {
        return {.available = true, .priority = 10};
    }

protected:
    CapabilityFuture<AssetDiscoveryContract::Result>
    invokeTyped(const AssetDiscoveryContract::Request& req, CapabilityContext& ctx) override {
        AssetDiscoveryContract::Result result;

        if (req.sourceType != "local.folder") return CapabilityFuture(result);

        std::filesystem::path root(req.uri);
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

        // ── Phase 2: find/create root folder asset ────────────────────────────
        std::string rootStr = root.string();
        std::string rootUri = "file://" + rootStr;
        std::string rootAssetId = assets_.findAssetByUri(rootUri);
        if (rootAssetId.empty()) {
            rootAssetId = assets_.createAsset(root.filename().string(), "folder");
            assets_.createAssetProvider(rootAssetId, pluginId_, rootUri);
            storeFilePath(meta_, pluginId_, rootAssetId, rootStr);
        }

        // ── Phase 3: build adjacency list in memory ───────────────────────────
        // path → assetId; deduplicates folder node creation across audio file paths.
        std::unordered_map<std::string, std::string> dirCache;
        dirCache[rootStr] = rootAssetId;

        std::unordered_map<std::string, std::vector<std::string>> childrenMap;
        std::unordered_map<std::string, std::unordered_set<std::string>> childrenSeen;

        auto addChild = [&](const std::string& parentId, const std::string& childId) {
            if (childrenSeen[parentId].insert(childId).second)
                childrenMap[parentId].push_back(childId);
        };

        for (auto& [filePath, kind] : audioFiles) {
            std::vector<std::filesystem::path> ancestors;
            auto cur = filePath.parent_path();
            while (cur.string() != rootStr && cur != cur.parent_path()) {
                ancestors.push_back(cur);
                cur = cur.parent_path();
            }
            std::reverse(ancestors.begin(), ancestors.end());

            std::string parentAssetId = rootAssetId;
            for (auto& ancestor : ancestors) {
                std::string ancestorPath = ancestor.string();
                auto it = dirCache.find(ancestorPath);
                std::string folderAssetId;
                if (it != dirCache.end()) {
                    folderAssetId = it->second;
                } else {
                    std::string uri = "file://" + ancestorPath;
                    folderAssetId = assets_.findAssetByUri(uri);
                    if (folderAssetId.empty()) {
                        folderAssetId = assets_.createAsset(
                            ancestor.filename().string(), "folder");
                        assets_.createAssetProvider(folderAssetId, pluginId_, uri);
                        storeFilePath(meta_, pluginId_, folderAssetId, ancestorPath);
                    }
                    dirCache[ancestorPath] = folderAssetId;
                    addChild(parentAssetId, folderAssetId);
                }
                parentAssetId = folderAssetId;
            }

            std::string fileUri = "file://" + filePath.string();
            std::string fileAssetId = assets_.findAssetByUri(fileUri);
            if (fileAssetId.empty()) {
                std::string name = filePath.stem().string();
                int64_t size = static_cast<int64_t>(std::filesystem::file_size(filePath, ec));
                fileAssetId = assets_.createAsset(name, kind);
                assets_.createAssetProvider(fileAssetId, pluginId_, fileUri);
                storeFilePath(meta_, pluginId_, fileAssetId, filePath.string());
                result.assets.push_back({name, pluginId_, {}, size});
            }
            addChild(parentAssetId, fileAssetId);
        }

        // ── Phase 4: merge with existing graph and persist (single blob) ────
        // V1 assumption: files are never deleted, so accumulated scan data stays valid.
        // When scanning a sub-folder that is already reachable from a previously-scanned
        // parent, HierarchyRootsHandler suppresses it from the roots list at query time.
        HierarchyGraph graph = loadHierarchyGraph(meta_, pluginId_);
        if (std::find(graph.roots.begin(), graph.roots.end(), rootAssetId) == graph.roots.end())
            graph.roots.push_back(rootAssetId);
        for (auto& [parent, kids] : childrenMap)  // overwrite this scan's adjacency entries
            graph.children[std::move(parent)] = std::move(kids);
        graph.version = Database::generateId();
        storeHierarchyGraph(meta_, pluginId_, graph);

        // ── Phase 5: rebuild the hierarchy cache ──────────────────────────────
        if (ctx.broker())
            hier_.traverse(pluginId_, *ctx.broker());

        return CapabilityFuture(result);
    }

private:
    std::string       pluginId_;
    AssetService&     assets_;
    MetadataService&  meta_;
    HierarchyService& hier_;
};

// builtin.asset.hierarchy.roots:v1
class HierarchyRootsHandler : public TypedCapabilityHandler<AssetHierarchyRootsContract> {
public:
    HierarchyRootsHandler(const std::string& pluginId, MetadataService& meta)
        : pluginId_(pluginId), meta_(meta) {}

    std::string_view providerId()      const override { return pluginId_; }
    ExecutionPolicy  executionPolicy() const override { return ExecutionPolicy::Sync; }
    CapabilityDescriptor describe(const CapabilityQuery&) override {
        return {.available = true, .priority = 10};
    }

protected:
    CapabilityFuture<AssetHierarchyRootsContract::Result>
    invokeTyped(const AssetHierarchyRootsContract::Request&, CapabilityContext&) override {
        auto g = loadHierarchyGraph(meta_, pluginId_);
        AssetHierarchyRootsContract::Result result;
        for (const auto& assetId : g.roots) {
            bool hasChildren = g.children.count(assetId) && !g.children.at(assetId).empty();
            result.nodes.push_back({assetId, assetId, hasChildren});
        }
        return CapabilityFuture(result);
    }

private:
    std::string      pluginId_;
    MetadataService& meta_;
};

// builtin.asset.hierarchy.children:v1
class HierarchyChildrenHandler : public TypedCapabilityHandler<AssetHierarchyChildrenContract> {
public:
    HierarchyChildrenHandler(const std::string& pluginId, MetadataService& meta)
        : pluginId_(pluginId), meta_(meta) {}

    std::string_view providerId()      const override { return pluginId_; }
    ExecutionPolicy  executionPolicy() const override { return ExecutionPolicy::Sync; }
    CapabilityDescriptor describe(const CapabilityQuery&) override {
        return {.available = true, .priority = 10};
    }

protected:
    CapabilityFuture<AssetHierarchyChildrenContract::Result>
    invokeTyped(const AssetHierarchyChildrenContract::Request& req, CapabilityContext&) override {
        auto g = loadHierarchyGraph(meta_, pluginId_);
        AssetHierarchyChildrenContract::Result result;
        auto it = g.children.find(req.nodeId);
        if (it != g.children.end()) {
            for (const auto& assetId : it->second) {
                bool hasChildren = g.children.count(assetId) && !g.children.at(assetId).empty();
                result.nodes.push_back({assetId, assetId, hasChildren});
            }
        }
        return CapabilityFuture(result);
    }

private:
    std::string      pluginId_;
    MetadataService& meta_;
};

// builtin.asset.hierarchy.version:v1
class HierarchyVersionHandler : public TypedCapabilityHandler<AssetHierarchyVersionContract> {
public:
    HierarchyVersionHandler(const std::string& pluginId, MetadataService& meta)
        : pluginId_(pluginId), meta_(meta) {}

    std::string_view providerId()      const override { return pluginId_; }
    ExecutionPolicy  executionPolicy() const override { return ExecutionPolicy::Sync; }
    CapabilityDescriptor describe(const CapabilityQuery&) override {
        return {.available = true, .priority = 10};
    }

protected:
    CapabilityFuture<AssetHierarchyVersionContract::Result>
    invokeTyped(const AssetHierarchyVersionContract::Request&, CapabilityContext&) override {
        return CapabilityFuture(AssetHierarchyVersionContract::Result{
            loadHierarchyGraph(meta_, pluginId_).version
        });
    }

private:
    std::string      pluginId_;
    MetadataService& meta_;
};

// builtin.provide_locator:v1
class LocatorHandler : public TypedCapabilityHandler<AssetLocatorContract> {
public:
    LocatorHandler(const std::string& pluginId, MetadataService& meta)
        : pluginId_(pluginId), meta_(meta) {}

    std::string_view providerId()      const override { return pluginId_; }
    ExecutionPolicy  executionPolicy() const override { return ExecutionPolicy::Sync; }

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

    std::string_view providerId()      const override { return pluginId_; }
    ExecutionPolicy  executionPolicy() const override { return ExecutionPolicy::Sync; }
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

    std::string_view providerId()      const override { return pluginId_; }
    ExecutionPolicy  executionPolicy() const override { return ExecutionPolicy::Sync; }
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

    std::string_view providerId()      const override { return pluginId_; }
    ExecutionPolicy  executionPolicy() const override { return ExecutionPolicy::Sync; }
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

    std::string_view providerId()      const override { return pluginId_; }
    ExecutionPolicy  executionPolicy() const override { return ExecutionPolicy::Sync; }
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
        QProcess::startDetached("open", {"-R", QString::fromStdString(path)});
#elif defined(_WIN32)
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
    LocalFileDiscoverTabHandler(const std::string& pluginId, AssetService& assets)
        : pluginId_(pluginId), assets_(assets) {}

    std::string_view providerId()      const override { return pluginId_; }
    ExecutionPolicy  executionPolicy() const override { return ExecutionPolicy::Sync; }
    CapabilityDescriptor describe(const CapabilityQuery&) override {
        return {.available = true, .priority = 10};
    }

protected:
    CapabilityFuture<DiscoverTabContract::Result>
    invokeTyped(const DiscoverTabContract::Request&, CapabilityContext& ctx) override {
        std::string       pid    = pluginId_;
        AssetService*     assets = &assets_;
        CapabilityBroker* broker = ctx.broker();

        return CapabilityFuture(DiscoverTabContract::Result{
            .tabId    = "builtin.local_file.discover",
            .label    = "Local Files",
            .priority = 10,
            .widgetFactory = [pid, assets, broker](QWidget* parent) -> QWidget* {
                return buildScanWidget(pid, assets, broker, parent);
            }
        });
    }

private:
    static QWidget* buildScanWidget(const std::string& pid, AssetService* assets,
                                    CapabilityBroker* broker, QWidget* parent) {
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
            [pid, broker, pathEdit, status, scanBtn]() {
                QString qpath = pathEdit->text().trimmed();
                if (qpath.isEmpty()) { status->setText("Enter a folder path first."); return; }
                if (!broker)         { status->setText("No broker available."); return; }

                status->setText("Scanning\xe2\x80\xa6");
                scanBtn->setEnabled(false);

                // Route through the capability bus so DiscoveryHandler runs in full
                // (hierarchy metadata + traverse are handled there).
                CapabilityRef ref;
                for (auto& r : broker->findAll<AssetDiscoveryContract>())
                    if (std::string(r.providerId) == pid) { ref = r; break; }

                if (!ref.valid()) {
                    status->setText("Discovery handler not registered.");
                    scanBtn->setEnabled(true);
                    return;
                }

                AssetDiscoveryContract::Request req;
                req.sourceType = "local.folder";
                req.uri        = qpath.toStdString();

                // DiscoveryHandler is Async — runs on WorkerPool; use onReady() so the
                // UI thread is never blocked. Jump back to the UI thread via invokeMethod.
                broker->invoke<AssetDiscoveryContract>(ref, req)
                    .onReady([status, scanBtn](const AssetDiscoveryContract::Result& result) {
                        QMetaObject::invokeMethod(status, [status, scanBtn,
                                                          count = result.assets.size()]() {
                            status->setText(
                                QString("Done. Found %1 new asset(s).").arg(count));
                            scanBtn->setEnabled(true);
                        });
                    });
            });

        return widget;
    }

    std::string   pluginId_;
    AssetService& assets_;
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
    h.push_back(std::make_unique<DiscoveryHandler>        (pluginId_, *assets_, *metadata_, *hierarchy_));
    h.push_back(std::make_unique<HierarchyRootsHandler>   (pluginId_, *metadata_));
    h.push_back(std::make_unique<HierarchyChildrenHandler>(pluginId_, *metadata_));
    h.push_back(std::make_unique<HierarchyVersionHandler> (pluginId_, *metadata_));
    h.push_back(std::make_unique<LocatorHandler>          (pluginId_, *metadata_));
    h.push_back(std::make_unique<NameHandler>             (pluginId_, *metadata_));
    h.push_back(std::make_unique<KindHandler>             (pluginId_, *metadata_));
    h.push_back(std::make_unique<OpenActionsHandler>             (pluginId_));
    h.push_back(std::make_unique<ExecuteActionHandler>           (pluginId_, *metadata_));
    h.push_back(std::make_unique<LocalFileDiscoverTabHandler>    (pluginId_, *assets_));
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
            "builtin.asset.hierarchy.roots:v1",
            "builtin.asset.hierarchy.children:v1",
            "builtin.asset.hierarchy.version:v1",
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
