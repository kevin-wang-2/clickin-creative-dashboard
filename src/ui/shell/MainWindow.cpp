#include "ui/shell/MainWindow.h"
#include "ui/asset_list/AssetListView.h"
#include "ui/plugin_mgmt/PluginManagementView.h"
#include "ui/inspector/InspectorPanel.h"
#include "ui/job_status/JobStatusBar.h"
#include "core/app/Application.h"

#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QWidget>

struct MainWindow::Impl {
    AssetListView*        assetList  = nullptr;
    InspectorPanel*       inspector  = nullptr;
    PluginManagementView* pluginMgmt = nullptr;
    JobStatusBar*         jobStatus  = nullptr;
};

MainWindow::MainWindow(clickin::Application& app, QWidget* parent)
    : QMainWindow(parent)
    , impl_(std::make_unique<Impl>())
{
    setWindowTitle("Clickin Creative Dashboard");
    resize(1280, 800);

    auto* tabs = new QTabWidget(this);

    // ── Assets tab ────────────────────────────────────────────────────────────
    auto* assetsPage   = new QWidget(tabs);
    auto* assetsLayout = new QVBoxLayout(assetsPage);
    assetsLayout->setContentsMargins(0, 0, 0, 0);

    auto* splitter    = new QSplitter(Qt::Horizontal, assetsPage);
    impl_->assetList  = new AssetListView(app, splitter);
    impl_->inspector  = new InspectorPanel(app, splitter);
    splitter->addWidget(impl_->assetList);
    splitter->addWidget(impl_->inspector);
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 1);

    assetsLayout->addWidget(splitter);
    tabs->addTab(assetsPage, "Assets");

    // ── Plugins tab ───────────────────────────────────────────────────────────
    impl_->pluginMgmt = new PluginManagementView(app, tabs);
    tabs->addTab(impl_->pluginMgmt, "Plugins");

    setCentralWidget(tabs);

    // ── Status bar ────────────────────────────────────────────────────────────
    impl_->jobStatus = new JobStatusBar(this);
    statusBar()->addWidget(impl_->jobStatus, 1);

    // ── Connections ───────────────────────────────────────────────────────────
    connect(impl_->assetList, &AssetListView::assetSelected,
            impl_->inspector, &InspectorPanel::onAssetSelected);
}

MainWindow::~MainWindow() = default;
