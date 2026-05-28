#pragma once

#include <QMainWindow>
#include <memory>

namespace clickin {
class Application;
}

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(clickin::Application& app, QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void showAssetDetails(const QString& assetId);
    void openDiscoverManager();

private:
    void buildMenuBar();
    void populatePluginMenuItems();

    struct Impl;
    std::unique_ptr<Impl> impl_;
};
