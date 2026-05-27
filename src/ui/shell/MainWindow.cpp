#include "ui/shell/MainWindow.h"
#include "core/app/Application.h"

#include <QTabWidget>
#include <QStatusBar>
#include <QLabel>

struct MainWindow::Impl {
    QTabWidget* tabs = nullptr;
};

MainWindow::MainWindow(clickin::Application& /*app*/, QWidget* parent)
    : QMainWindow(parent)
    , impl_(std::make_unique<Impl>())
{
    setWindowTitle("Clickin Creative Dashboard");
    resize(1200, 750);

    impl_->tabs = new QTabWidget(this);
    impl_->tabs->addTab(new QLabel("Asset list — Phase 4", impl_->tabs), "Assets");
    impl_->tabs->addTab(new QLabel("Plugin management — Phase 4", impl_->tabs), "Plugins");
    setCentralWidget(impl_->tabs);

    statusBar()->showMessage("Ready");
}

MainWindow::~MainWindow() = default;
