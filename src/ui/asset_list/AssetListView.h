#pragma once
#include <QWidget>
#include <memory>

namespace clickin { class Application; }

class AssetListView : public QWidget {
    Q_OBJECT
public:
    explicit AssetListView(clickin::Application& app, QWidget* parent = nullptr);
    ~AssetListView();

signals:
    void assetSelected(const QString& assetId);
    void showDetailsRequested(const QString& assetId);

public slots:
    void refresh();
    void onScanFolder();

private slots:
    void onContextMenuRequested(const QPoint& pos);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
