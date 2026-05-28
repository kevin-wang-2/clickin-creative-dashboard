#include "ui/asset_list/AssetListView.h"
#include "core/app/Application.h"
#include "core/app/CoreContext.h"
#include "core/services/AssetService.h"
#include "core/services/HierarchyService.h"
#include "core/capability/CapabilityBroker.h"
#include "sdk/contracts/builtin/AssetDiscoveryContract.h"
#include "sdk/contracts/builtin/AssetKindContract.h"
#include "sdk/contracts/builtin/AssetOpenActionsContract.h"
#include "sdk/contracts/builtin/AssetRef.h"
#include "sdk/contracts/builtin/AssetSearchContract.h"
#include <QPointer>

#include <QAbstractTableModel>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
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

    QString kindAt(int row) const {
        if (row < 0 || row >= static_cast<int>(assets_.size())) return {};
        return QString::fromStdString(assets_[row].kind);
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

struct NavEntry {
    QString assetId;
    QString name;
};

struct AssetListView::Impl {
    clickin::Application& app;
    AssetTableModel* model         = nullptr;
    QTableView*      table         = nullptr;
    QLineEdit*       searchBar     = nullptr;
    QTimer*          debounceTimer = nullptr;
    QWidget*         breadcrumb    = nullptr;   // container rebuilt on each navigation

    std::vector<NavEntry> navStack;  // empty = flat root view

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

    // Breadcrumb placeholder — rebuilt in rebuildBreadcrumb()
    impl_->breadcrumb = new QWidget(this);
    impl_->breadcrumb->setLayout(new QHBoxLayout());
    impl_->breadcrumb->layout()->setContentsMargins(0, 0, 0, 0);
    impl_->breadcrumb->hide();
    layout->addWidget(impl_->breadcrumb);

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
    // Stay at current nav level; reload from DB.
    loadCurrentLevel();
}

void AssetListView::loadCurrentLevel() {
    auto ctx = impl_->app.coreContext();

    std::vector<clickin::AssetRecord> assets;

    if (impl_->navStack.empty()) {
        // Root: prefer hierarchy nodes, fall back to flat list.
        auto nodes = ctx.hierarchy.getAllNodes();
        if (!nodes.empty()) {
            assets = std::move(nodes);
        } else {
            assets = ctx.assets.listAssets();
            // Backfill kind for assets that predate the kind column.
            auto kindRef = ctx.capabilities.findBest<clickin::AssetKindContract>(
                clickin::CapabilityQuery{});
            if (kindRef.valid()) {
                for (auto& a : assets) {
                    if (!a.kind.empty()) continue;
                    auto result = ctx.capabilities
                        .invoke<clickin::AssetKindContract>(kindRef, clickin::AssetRef{a.id})
                        .get();
                    if (!result.kind.empty() && result.confidence > 0) {
                        ctx.assets.setAssetKind(a.id, result.kind);
                        a.kind = result.kind;
                    }
                }
            }
        }
    } else {
        const QString& parentId = impl_->navStack.back().assetId;
        assets = ctx.hierarchy.getAllChildren(parentId.toStdString());
    }

    impl_->model->setAssets(std::move(assets));
}

void AssetListView::navigateInto(const QString& assetId, const QString& name) {
    if (static_cast<int>(impl_->navStack.size()) >= kMaxNavDepth) return;
    impl_->navStack.push_back({assetId, name});
    rebuildBreadcrumb();
    impl_->searchBar->clear();
    loadCurrentLevel();
}

void AssetListView::navigateTo(int depth) {
    if (depth < 0) depth = 0;
    impl_->navStack.resize(static_cast<std::size_t>(depth));
    rebuildBreadcrumb();
    impl_->searchBar->clear();
    loadCurrentLevel();
}

void AssetListView::rebuildBreadcrumb() {
    // Remove old buttons.
    auto* bcLayout = qobject_cast<QHBoxLayout*>(impl_->breadcrumb->layout());
    while (bcLayout->count() > 0) {
        auto* item = bcLayout->takeAt(0);
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }

    if (impl_->navStack.empty()) {
        impl_->breadcrumb->hide();
        return;
    }

    // Root button
    auto* rootBtn = new QPushButton("⌂ All", impl_->breadcrumb);
    rootBtn->setFlat(true);
    connect(rootBtn, &QPushButton::clicked, this, [this]() { navigateTo(0); });
    bcLayout->addWidget(rootBtn);

    for (int i = 0; i < static_cast<int>(impl_->navStack.size()); ++i) {
        auto* sep = new QLabel(" › ", impl_->breadcrumb);
        bcLayout->addWidget(sep);

        const int depth = i + 1;
        const QString name = impl_->navStack[i].name;
        auto* btn = new QPushButton(name, impl_->breadcrumb);
        btn->setFlat(true);
        // Only the last entry is non-clickable (current location)
        if (depth == static_cast<int>(impl_->navStack.size())) {
            btn->setEnabled(false);
        } else {
            connect(btn, &QPushButton::clicked, this, [this, depth]() { navigateTo(depth); });
        }
        bcLayout->addWidget(btn);
    }

    bcLayout->addStretch();
    impl_->breadcrumb->show();
}

void AssetListView::onBreadcrumbClicked(int depth) {
    navigateTo(depth);
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
                if (impl_->model->kindAt(idx.row()) == "folder") {
                    QModelIndex nameIdx = impl_->model->index(idx.row(), 0);
                    navigateInto(impl_->model->assetIdAt(idx.row()),
                                 impl_->model->data(nameIdx, Qt::DisplayRole).toString());
                } else {
                    emit previewRequested(impl_->model->assetIdAt(idx.row()));
                }
                return true;
            }
        }
    }
    return QWidget::eventFilter(obj, ev);
}

void AssetListView::onDoubleClicked(const QModelIndex& index) {
    if (!index.isValid()) return;
    QString assetId = impl_->model->assetIdAt(index.row());
    if (impl_->model->kindAt(index.row()) == "folder") {
        QModelIndex nameIdx = impl_->model->index(index.row(), 0);
        QString name = impl_->model->data(nameIdx, Qt::DisplayRole).toString();
        navigateInto(assetId, name);
    } else {
        emit previewRequested(assetId);
    }
}

void AssetListView::onContextMenuRequested(const QPoint& pos) {
    QModelIndex idx = impl_->table->indexAt(pos);
    if (!idx.isValid()) return;

    QString assetId = impl_->model->assetIdAt(idx.row());
    bool isFolder   = (impl_->model->kindAt(idx.row()) == "folder");

    QMenu menu(this);

    // Folders: navigate; non-folders: preview.
    QAction* previewAct = nullptr;
    QAction* openFolderAct = nullptr;
    if (isFolder) {
        openFolderAct = menu.addAction("Open");
    } else {
        previewAct = menu.addAction("Preview");
        previewAct->setShortcut(Qt::Key_Space);
    }

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

    if (chosen == openFolderAct) {
        QModelIndex nameIdx = impl_->model->index(idx.row(), 0);
        navigateInto(assetId, impl_->model->data(nameIdx, Qt::DisplayRole).toString());
        return;
    }
    if (chosen == previewAct) {
        emit previewRequested(assetId);
        return;
    }
    if (chosen == detailsAct) {
        emit showDetailsRequested(assetId);
        return;
    }

    // Plugin-contributed action — look up type and invoke.
    QString chosenId = chosen->data().toString();
    clickin::AssetAction::Type actionType = clickin::AssetAction::Type::Execute;
    for (const auto& a : openActions)
        if (QString::fromStdString(a.id) == chosenId) { actionType = a.type; break; }

    auto execRef = ctx.capabilities.findBest<clickin::AssetExecuteActionContract>(
        clickin::CapabilityQuery{});
    if (!execRef.valid()) return;

    clickin::AssetExecuteActionContract::Request execReq{ref, chosenId.toStdString()};
    auto result = ctx.capabilities
        .invoke<clickin::AssetExecuteActionContract>(execRef, execReq).get();

    if (actionType == clickin::AssetAction::Type::OpenWindow && result.windowFactory) {
        QWidget* win = result.windowFactory(nullptr);
        if (win) {
            win->setAttribute(Qt::WA_DeleteOnClose);
            win->setWindowFlag(Qt::Window);
            win->show();
        }
    } else if (!result.success && !result.errorMessage.empty()) {
        QMessageBox::warning(this, "Action failed",
                             QString::fromStdString(result.errorMessage));
    }
}

void AssetListView::onSearchTextChanged(const QString&) {
    impl_->debounceTimer->start();
}

void AssetListView::onSearchDebounced() {
    QString query = impl_->searchBar->text().trimmed();
    if (query.isEmpty()) {
        loadCurrentLevel();
        return;
    }

    // Search always operates on the full flat catalogue; collapse nav stack visually.
    auto ctx = impl_->app.coreContext();
    auto ref = ctx.capabilities.findBest<clickin::AssetSearchContract>(clickin::CapabilityQuery{});
    if (!ref.valid()) {
        loadCurrentLevel();
        return;
    }

    clickin::AssetSearchContract::Request req;
    req.query = query.toStdString();
    auto results = ctx.capabilities
        .invoke<clickin::AssetSearchContract>(ref, req).get();
    impl_->model->setAssets(std::move(results.assets));
}
