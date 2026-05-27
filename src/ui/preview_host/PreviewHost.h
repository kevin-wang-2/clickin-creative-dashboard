#pragma once
#include <QWidget>
#include <memory>

namespace clickin { class Application; }

class PreviewHost : public QWidget {
    Q_OBJECT
public:
    explicit PreviewHost(clickin::Application& app, QWidget* parent = nullptr);
    ~PreviewHost();

public slots:
    void onAssetSelected(const QString& assetId);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
