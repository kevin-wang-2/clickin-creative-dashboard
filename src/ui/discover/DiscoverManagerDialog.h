#pragma once
#include <QDialog>
#include <functional>
#include <memory>
#include <unordered_map>

class QTabWidget;
namespace clickin { class Application; }

class DiscoverManagerDialog : public QDialog {
    Q_OBJECT
public:
    explicit DiscoverManagerDialog(clickin::Application& app, QWidget* parent = nullptr);
    ~DiscoverManagerDialog() override;

private slots:
    void onTabChanged(int index);

private:
    clickin::Application& app_;
    QTabWidget* tabs_ = nullptr;
    std::unordered_map<QWidget*, std::function<QWidget*(QWidget*)>> lazyFactories_;
};
