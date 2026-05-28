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
    void onTabChanged(int index);

private:
    void buildMenuBar();
    void populatePluginMenuItems();
    void populatePluginTabs();

    struct Impl;
    std::unique_ptr<Impl> impl_;
};
