#pragma once
#include <QWidget>
#include <memory>

namespace clickin { class Application; }

class PluginManagementView : public QWidget {
    Q_OBJECT
public:
    explicit PluginManagementView(clickin::Application& app, QWidget* parent = nullptr);
    ~PluginManagementView();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
