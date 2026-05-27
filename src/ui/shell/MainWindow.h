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

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
