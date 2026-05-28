#include "ui/shell/MainWindow.h"
#include "ui/asset_list/AssetListView.h"
#include "ui/plugin_mgmt/PluginManagementView.h"
#include "ui/inspector/InspectorPanel.h"
#include "ui/preview_host/PreviewHost.h"
#include "ui/job_status/JobStatusBar.h"
#include "core/app/Application.h"
#include "core/app/CoreContext.h"
#include "sdk/contracts/builtin/MenuBarItemsContract.h"
#include "sdk/contracts/builtin/DiscoverTabContract.h"

#include <QDialog>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <unordered_map>

struct MainWindow::Impl {
    clickin::Application& app;

    AssetListView*        assetList  = nullptr;
    PreviewHost*          preview    = nullptr;
    PluginManagementView* pluginMgmt = nullptr;
    JobStatusBar*         jobStatus  = nullptr;
    QTabWidget*           tabs       = nullptr;

    // Inspector lives in a modeless dialog — created on first use.
    QDialog*        detailDialog = nullptr;
    InspectorPanel* inspector    = nullptr;

    // Lazy-create plugin-contributed discover tabs: placeholder widget → factory.
    std::unordered_map<QWidget*, std::function<QWidget*(QWidget*)>> lazyTabFactories;

    explicit Impl(clickin::Application& a) : app(a) {}
};

MainWindow::MainWindow(clickin::Application& app, QWidget* parent)
    : QMainWindow(parent)
    , impl_(std::make_unique<Impl>(app))
{
    setWindowTitle("Clickin Creative Dashboard");
    resize(1280, 800);

    impl_->tabs = new QTabWidget(this);

    // ── Assets tab: vertical splitter (list top, preview bottom) ─────────────
    auto* assetsPage   = new QWidget(this);
    auto* assetsLayout = new QVBoxLayout(assetsPage);
    assetsLayout->setContentsMargins(0, 0, 0, 0);

    auto* splitter = new QSplitter(Qt::Vertical, assetsPage);
    impl_->assetList = new AssetListView(app, splitter);
    impl_->preview   = new PreviewHost(app, splitter);
    splitter->addWidget(impl_->assetList);
    splitter->addWidget(impl_->preview);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);

    assetsLayout->addWidget(splitter);
    impl_->tabs->addTab(assetsPage, "Assets");  // priority 0, always tab 0

    // ── Plugin-contributed discover tabs (inserted before Plugins tab) ────────
    populatePluginTabs();

    // ── Plugins tab (always last) ─────────────────────────────────────────────
    impl_->pluginMgmt = new PluginManagementView(app, this);
    impl_->tabs->addTab(impl_->pluginMgmt, "Plugins");

    setCentralWidget(impl_->tabs);

    // ── Status bar ────────────────────────────────────────────────────────────
    impl_->jobStatus = new JobStatusBar(this);
    statusBar()->addWidget(impl_->jobStatus, 1);

    // ── Menu bar ──────────────────────────────────────────────────────────────
    buildMenuBar();
    populatePluginMenuItems();

    // ── Connections ───────────────────────────────────────────────────────────
    connect(impl_->tabs, &QTabWidget::currentChanged,
            this,        &MainWindow::onTabChanged);
    connect(impl_->assetList, &AssetListView::previewRequested,
            impl_->preview,   &PreviewHost::onAssetSelected);
    connect(impl_->assetList, &AssetListView::showDetailsRequested,
            this,             &MainWindow::showAssetDetails);
}

MainWindow::~MainWindow() = default;

void MainWindow::buildMenuBar() {
    QMenu* discoveryMenu = menuBar()->addMenu("Discovery");

    QAction* scanAct = discoveryMenu->addAction("Scan Folder\xe2\x80\xa6");
    scanAct->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O));
    connect(scanAct, &QAction::triggered, impl_->assetList, &AssetListView::onScanFolder);

    QAction* refreshAct = discoveryMenu->addAction("Refresh");
    refreshAct->setShortcut(QKeySequence::Refresh);
    connect(refreshAct, &QAction::triggered, impl_->assetList, &AssetListView::refresh);

    QMenu* pluginMenu = menuBar()->addMenu("Plugin");
    QAction* showPluginsAct = pluginMenu->addAction("Plugin Management");
    connect(showPluginsAct, &QAction::triggered, this, [this]() {
        impl_->tabs->setCurrentWidget(impl_->pluginMgmt);
    });
}

void MainWindow::onTabChanged(int index) {
    QWidget* placeholder = impl_->tabs->widget(index);
    auto it = impl_->lazyTabFactories.find(placeholder);
    if (it == impl_->lazyTabFactories.end()) return;

    auto factory = std::move(it->second);
    impl_->lazyTabFactories.erase(it);

    QWidget* real = factory(placeholder);
    auto* layout = new QVBoxLayout(placeholder);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(real);
}

void MainWindow::populatePluginMenuItems() {
    auto ctx  = impl_->app.coreContext();
    auto refs = ctx.capabilities.findAll<clickin::MenuBarItemsContract>();

    for (const auto& ref : refs) {
        auto result = ctx.capabilities
            .invoke<clickin::MenuBarItemsContract>(ref, clickin::MenuBarItemsContract::Request{})
            .get();

        for (const auto& item : result.items) {
            // Find or create the named menu.
            QString menuName = QString::fromStdString(item.menuName);
            QMenu* targetMenu = nullptr;
            for (QAction* a : menuBar()->actions()) {
                if (a->menu() && a->text() == menuName) {
                    targetMenu = a->menu();
                    break;
                }
            }
            if (!targetMenu) {
                targetMenu = menuBar()->addMenu(menuName);
            }

            QAction* act = targetMenu->addAction(QString::fromStdString(item.label));
            if (!item.shortcut.empty())
                act->setShortcut(QKeySequence(QString::fromStdString(item.shortcut)));

            auto callback = item.action;
            connect(act, &QAction::triggered, this, [this, callback]() {
                if (!callback) {
                    QMessageBox::warning(this, "Plugin Error",
                                         "Action has no handler registered.");
                    return;
                }
                callback();
            });
        }
    }
}

void MainWindow::populatePluginTabs() {
    auto ctx  = impl_->app.coreContext();
    auto refs = ctx.capabilities.findAll<clickin::DiscoverTabContract>();

    // Collect all tab descriptors.
    struct TabDesc {
        std::string label;
        int         priority;
        std::function<QWidget*(QWidget*)> factory;
    };
    std::vector<TabDesc> descs;
    for (const auto& ref : refs) {
        auto result = ctx.capabilities
            .invoke<clickin::DiscoverTabContract>(ref, clickin::DiscoverTabContract::Request{})
            .get();
        if (!result.label.empty() && result.widgetFactory)
            descs.push_back({result.label, result.priority, result.widgetFactory});
    }

    // Sort by priority ascending (lower = further left).
    std::sort(descs.begin(), descs.end(),
              [](const TabDesc& a, const TabDesc& b) { return a.priority < b.priority; });

    // Insert each tab at the current end (before Plugins, which is added after).
    for (auto& desc : descs) {
        auto* placeholder = new QWidget(this);
        impl_->tabs->addTab(placeholder, QString::fromStdString(desc.label));
        impl_->lazyTabFactories[placeholder] = std::move(desc.factory);
    }
}

void MainWindow::showAssetDetails(const QString& assetId) {
    if (!impl_->detailDialog) {
        impl_->detailDialog = new QDialog(this);
        impl_->detailDialog->setWindowTitle("Asset Details");
        impl_->detailDialog->setWindowFlags(
            impl_->detailDialog->windowFlags() | Qt::Tool);
        impl_->inspector = new InspectorPanel(impl_->app, impl_->detailDialog);
        auto* layout = new QVBoxLayout(impl_->detailDialog);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(impl_->inspector);
        impl_->detailDialog->resize(380, 320);
    }

    impl_->inspector->onAssetSelected(assetId);
    impl_->detailDialog->show();
    impl_->detailDialog->raise();
    impl_->detailDialog->activateWindow();
}
