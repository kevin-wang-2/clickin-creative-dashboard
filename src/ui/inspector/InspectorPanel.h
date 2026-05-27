#pragma once
#include <QWidget>
#include <memory>

namespace clickin { class Application; }

class InspectorPanel : public QWidget {
    Q_OBJECT
public:
    explicit InspectorPanel(clickin::Application& app, QWidget* parent = nullptr);
    ~InspectorPanel();

public slots:
    void onAssetSelected(const QString& assetId);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
