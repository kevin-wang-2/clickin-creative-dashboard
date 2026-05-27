#include "ui/asset_list/AssetListView.h"
#include "core/app/Application.h"
#include "core/app/CoreContext.h"
#include "core/services/AssetService.h"
#include "core/capability/CapabilityBroker.h"
#include "sdk/contracts/builtin/AssetDiscoveryContract.h"

#include <QAbstractTableModel>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableView>
#include <QVBoxLayout>

// ── Asset table model ─────────────────────────────────────────────────────────

class AssetTableModel : public QAbstractTableModel {
public:
    explicit AssetTableModel(QObject* parent = nullptr)
        : QAbstractTableModel(parent) {}

    void setAssets(std::vector<clickin::AssetRecord> assets) {
        beginResetModel();
        assets_ = std::move(assets);
        endResetModel();
    }

    QString assetIdAt(int row) const {
        if (row < 0 || row >= static_cast<int>(assets_.size())) return {};
        return QString::fromStdString(assets_[row].id);
    }

    int rowCount(const QModelIndex& = {}) const override {
        return static_cast<int>(assets_.size());
    }
    int columnCount(const QModelIndex& = {}) const override { return 3; }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override {
        if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
        switch (section) {
            case 0: return "Name";
            case 1: return "Status";
            case 2: return "ID";
        }
        return {};
    }

    QVariant data(const QModelIndex& index, int role) const override {
        if (!index.isValid() || role != Qt::DisplayRole) return {};
        const auto& a = assets_[index.row()];
        switch (index.column()) {
            case 0: return QString::fromStdString(a.name);
            case 1: return QString::fromStdString(a.status);
            case 2: return QString::fromStdString(a.id);
        }
        return {};
    }

private:
    std::vector<clickin::AssetRecord> assets_;
};

// ── Impl ──────────────────────────────────────────────────────────────────────

struct AssetListView::Impl {
    clickin::Application& app;
    AssetTableModel* model = nullptr;
    QTableView*      table = nullptr;

    explicit Impl(clickin::Application& a) : app(a) {}
};

// ── AssetListView ─────────────────────────────────────────────────────────────

AssetListView::AssetListView(clickin::Application& app, QWidget* parent)
    : QWidget(parent)
    , impl_(std::make_unique<Impl>(app))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* toolbar       = new QWidget(this);
    auto* toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(4, 4, 4, 4);

    auto* scanBtn    = new QPushButton("Scan Folder…", toolbar);
    auto* refreshBtn = new QPushButton("Refresh", toolbar);
    toolbarLayout->addWidget(scanBtn);
    toolbarLayout->addWidget(refreshBtn);
    toolbarLayout->addStretch();

    impl_->model = new AssetTableModel(this);
    impl_->table = new QTableView(this);
    impl_->table->setModel(impl_->model);
    impl_->table->setSelectionBehavior(QAbstractItemView::SelectRows);
    impl_->table->setSelectionMode(QAbstractItemView::SingleSelection);
    impl_->table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    impl_->table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    impl_->table->verticalHeader()->hide();

    layout->addWidget(toolbar);
    layout->addWidget(impl_->table, 1);

    connect(scanBtn,    &QPushButton::clicked, this, &AssetListView::onScanFolder);
    connect(refreshBtn, &QPushButton::clicked, this, &AssetListView::refresh);
    connect(impl_->table->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, [this](const QModelIndex& current, const QModelIndex&) {
        if (!current.isValid()) return;
        emit assetSelected(impl_->model->assetIdAt(current.row()));
    });

    refresh();
}

AssetListView::~AssetListView() = default;

void AssetListView::refresh() {
    auto ctx    = impl_->app.coreContext();
    auto assets = ctx.assets.listAssets();
    impl_->model->setAssets(std::move(assets));
}

void AssetListView::onScanFolder() {
    QString dir = QFileDialog::getExistingDirectory(
        this, "Select folder to scan", QDir::homePath());
    if (dir.isEmpty()) return;

    auto ctx = impl_->app.coreContext();
    auto ref = ctx.capabilities.findBest<clickin::AssetDiscoveryContract>(clickin::CapabilityQuery{});
    if (!ref.valid()) {
        QMessageBox::warning(this, "Error", "No asset discovery handler available.");
        return;
    }

    clickin::AssetDiscoveryContract::Request req;
    req.sourceType = "local.folder";
    req.uri        = dir.toStdString();
    ctx.capabilities.invoke<clickin::AssetDiscoveryContract>(ref, req).get();

    refresh();
}
