#include "ui/shell/MainWindow.h"
#include "ui/asset_list/AssetListView.h"
#include "ui/plugin_mgmt/PluginManagementView.h"
#include "ui/inspector/InspectorPanel.h"
#include "ui/preview_host/PreviewHost.h"
#include "ui/job_status/JobStatusBar.h"
#include "core/app/Application.h"

#include <QDialog>
#include <QMenuBar>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QVBoxLayout>

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
    impl_->tabs->addTab(assetsPage, "Assets");

    // ── Plugins tab ───────────────────────────────────────────────────────────
    impl_->pluginMgmt = new PluginManagementView(app, this);
    impl_->tabs->addTab(impl_->pluginMgmt, "Plugins");

    setCentralWidget(impl_->tabs);

    // ── Status bar ────────────────────────────────────────────────────────────
    impl_->jobStatus = new JobStatusBar(this);
    statusBar()->addWidget(impl_->jobStatus, 1);

    // ── Menu bar ──────────────────────────────────────────────────────────────
    buildMenuBar();

    // ── Connections ───────────────────────────────────────────────────────────
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
