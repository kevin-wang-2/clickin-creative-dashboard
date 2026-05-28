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
    void previewRequested(const QString& assetId);   // Space or double-click
    void showDetailsRequested(const QString& assetId);

public slots:
    void refresh();
    void onScanFolder();

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;

private slots:
    void onContextMenuRequested(const QPoint& pos);
    void onDoubleClicked(const QModelIndex& index);
    void onSearchTextChanged(const QString& text);
    void onSearchDebounced();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
