#include "ui/asset_list/AssetListView.h"
#include "core/app/Application.h"
#include "core/app/CoreContext.h"
#include "core/services/AssetService.h"
#include "core/capability/CapabilityBroker.h"
#include "sdk/contracts/builtin/AssetDiscoveryContract.h"
#include "sdk/contracts/builtin/AssetKindContract.h"
#include "sdk/contracts/builtin/AssetOpenActionsContract.h"
#include "sdk/contracts/builtin/AssetRef.h"
#include "sdk/contracts/builtin/AssetSearchContract.h"

#include <QAbstractTableModel>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QTableView>
#include <QTimer>
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
            case 1: return "Type";
            case 2: return "Status";
        }
        return {};
    }

    QVariant data(const QModelIndex& index, int role) const override {
        if (!index.isValid() || role != Qt::DisplayRole) return {};
        const auto& a = assets_[index.row()];
        switch (index.column()) {
            case 0: return QString::fromStdString(a.name);
            case 1: return a.kind.empty() ? QString("—") : QString::fromStdString(a.kind);
            case 2: return QString::fromStdString(a.status);
        }
        return {};
    }

private:
    std::vector<clickin::AssetRecord> assets_;
};

// ── Impl ──────────────────────────────────────────────────────────────────────

struct AssetListView::Impl {
    clickin::Application& app;
    AssetTableModel* model         = nullptr;
    QTableView*      table         = nullptr;
    QLineEdit*       searchBar     = nullptr;
    QTimer*          debounceTimer = nullptr;

    explicit Impl(clickin::Application& a) : app(a) {}
};

// ── AssetListView ─────────────────────────────────────────────────────────────

AssetListView::AssetListView(clickin::Application& app, QWidget* parent)
    : QWidget(parent)
    , impl_(std::make_unique<Impl>(app))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    impl_->searchBar = new QLineEdit(this);
    impl_->searchBar->setPlaceholderText("Search assets…");
    impl_->searchBar->setClearButtonEnabled(true);
    layout->addWidget(impl_->searchBar);

    impl_->debounceTimer = new QTimer(this);
    impl_->debounceTimer->setSingleShot(true);
    impl_->debounceTimer->setInterval(300);

    impl_->model = new AssetTableModel(this);
    impl_->table = new QTableView(this);
    impl_->table->setModel(impl_->model);
    impl_->table->setSelectionBehavior(QAbstractItemView::SelectRows);
    impl_->table->setSelectionMode(QAbstractItemView::SingleSelection);
    impl_->table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    impl_->table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    impl_->table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    impl_->table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    impl_->table->verticalHeader()->hide();
    impl_->table->setContextMenuPolicy(Qt::CustomContextMenu);

    layout->addWidget(impl_->table);

    impl_->table->installEventFilter(this);

    connect(impl_->table->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, [this](const QModelIndex& current, const QModelIndex&) {
        if (!current.isValid()) return;
        emit assetSelected(impl_->model->assetIdAt(current.row()));
    });
    connect(impl_->table, &QTableView::doubleClicked,
            this, &AssetListView::onDoubleClicked);
    connect(impl_->table, &QTableView::customContextMenuRequested,
            this, &AssetListView::onContextMenuRequested);
    connect(impl_->searchBar, &QLineEdit::textChanged,
            this, &AssetListView::onSearchTextChanged);
    connect(impl_->debounceTimer, &QTimer::timeout,
            this, &AssetListView::onSearchDebounced);

    refresh();
}

AssetListView::~AssetListView() = default;

void AssetListView::refresh() {
    auto ctx    = impl_->app.coreContext();
    auto assets = ctx.assets.listAssets();

    // Backfill kind for assets that predate the kind column.
    // Once stored it won't be re-queried on subsequent refreshes.
    auto kindRef = ctx.capabilities.findBest<clickin::AssetKindContract>(clickin::CapabilityQuery{});
    if (kindRef.valid()) {
        for (auto& a : assets) {
            if (!a.kind.empty()) continue;
            auto result = ctx.capabilities
                .invoke<clickin::AssetKindContract>(kindRef, clickin::AssetRef{a.id}).get();
            if (!result.kind.empty() && result.confidence > 0) {
                ctx.assets.setAssetKind(a.id, result.kind);
                a.kind = result.kind;
            }
        }
    }

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

bool AssetListView::eventFilter(QObject* obj, QEvent* ev) {
    if (obj == impl_->table && ev->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(ev);
        if (ke->key() == Qt::Key_Space) {
            auto idx = impl_->table->currentIndex();
            if (idx.isValid()) {
                emit previewRequested(impl_->model->assetIdAt(idx.row()));
                return true;
            }
        }
    }
    return QWidget::eventFilter(obj, ev);
}

void AssetListView::onDoubleClicked(const QModelIndex& index) {
    if (!index.isValid()) return;
    emit previewRequested(impl_->model->assetIdAt(index.row()));
}

void AssetListView::onContextMenuRequested(const QPoint& pos) {
    QModelIndex idx = impl_->table->indexAt(pos);
    if (!idx.isValid()) return;

    QString assetId = impl_->model->assetIdAt(idx.row());

    QMenu menu(this);

    // Default preview action (always shown)
    QAction* previewAct = menu.addAction("Preview");
    previewAct->setShortcut(Qt::Key_Space);

    menu.addSeparator();
    QAction* detailsAct = menu.addAction("Show Details");

    // Open actions from AssetOpenActionsContract
    auto ctx = impl_->app.coreContext();
    clickin::AssetRef ref{assetId.toStdString(), ""};
    auto actRef = ctx.capabilities.findBest<clickin::AssetOpenActionsContract>(
        clickin::CapabilityQuery{});
    std::vector<clickin::AssetAction> openActions;
    if (actRef.valid()) {
        openActions = ctx.capabilities
            .invoke<clickin::AssetOpenActionsContract>(actRef, ref)
            .get().actions;
        if (!openActions.empty()) {
            menu.addSeparator();
            for (const auto& a : openActions)
                menu.addAction(QString::fromStdString(a.label))
                    ->setData(QString::fromStdString(a.id));
        }
    }

    QAction* chosen = menu.exec(impl_->table->viewport()->mapToGlobal(pos));
    if (!chosen) return;

    if (chosen == previewAct) {
        emit previewRequested(assetId);
    } else if (chosen == detailsAct) {
        emit showDetailsRequested(assetId);
    }
    // (future: invoke AssetExecuteActionContract for other actions)
}

void AssetListView::onSearchTextChanged(const QString&) {
    impl_->debounceTimer->start();
}

void AssetListView::onSearchDebounced() {
    QString query = impl_->searchBar->text().trimmed();
    if (query.isEmpty()) {
        refresh();
        return;
    }

    auto ctx    = impl_->app.coreContext();
    auto ref    = ctx.capabilities.findBest<clickin::AssetSearchContract>(clickin::CapabilityQuery{});
    if (!ref.valid()) {
        refresh();
        return;
    }

    clickin::AssetSearchContract::Request req;
    req.query = query.toStdString();
    auto results = ctx.capabilities
        .invoke<clickin::AssetSearchContract>(ref, req).get();
    impl_->model->setAssets(std::move(results.assets));
}
