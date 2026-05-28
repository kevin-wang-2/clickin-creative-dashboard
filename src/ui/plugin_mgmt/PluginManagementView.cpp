#include "ui/plugin_mgmt/PluginManagementView.h"
#include "core/app/Application.h"

#include <QHeaderView>
#include <QMap>
#include <QPointer>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

struct PluginManagementView::Impl {
    clickin::Application&          app;
    QTableWidget*                  table = nullptr;
    QMap<QString, QPointer<QWidget>> openWindows;

    explicit Impl(clickin::Application& a) : app(a) {}
};

PluginManagementView::~PluginManagementView() = default;

PluginManagementView::PluginManagementView(clickin::Application& app, QWidget* parent)
    : QWidget(parent)
    , impl_(std::make_unique<Impl>(app))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    impl_->table = new QTableWidget(this);
    impl_->table->setColumnCount(7);
    impl_->table->setHorizontalHeaderLabels(
        {"Plugin ID", "Name", "Version", "Status", "Built-in", "Critical", "Window"});
    impl_->table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    impl_->table->setSelectionBehavior(QAbstractItemView::SelectRows);
    impl_->table->setSelectionMode(QAbstractItemView::SingleSelection);
    impl_->table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    impl_->table->verticalHeader()->hide();
    impl_->table->setToolTip("Double-click a plugin with a window to open it");

    layout->addWidget(impl_->table);

    const auto states = impl_->app.pluginStates();
    impl_->table->setRowCount(static_cast<int>(states.size()));
    for (int row = 0; row < static_cast<int>(states.size()); ++row) {
        const auto& s = states[row];
        impl_->table->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(s.pluginId)));
        impl_->table->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(s.name)));
        impl_->table->setItem(row, 2, new QTableWidgetItem(QString::fromStdString(s.version)));
        impl_->table->setItem(row, 3, new QTableWidgetItem(QString::fromStdString(s.loadStatus)));
        impl_->table->setItem(row, 4, new QTableWidgetItem(s.builtin  ? "Yes" : "No"));
        impl_->table->setItem(row, 5, new QTableWidgetItem(s.critical ? "Yes" : "No"));
        impl_->table->setItem(row, 6, new QTableWidgetItem(s.hasWindow ? "Yes" : "—"));
    }
    impl_->table->resizeColumnsToContents();
    impl_->table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);

    connect(impl_->table, &QTableWidget::cellDoubleClicked,
            this, &PluginManagementView::onItemDoubleClicked);
}

void PluginManagementView::onItemDoubleClicked(int row, int /*column*/) {
    auto* idItem = impl_->table->item(row, 0);
    if (!idItem) return;

    QString pluginId = idItem->text();

    // Raise existing window if still alive.
    auto it = impl_->openWindows.find(pluginId);
    if (it != impl_->openWindows.end() && !it->isNull()) {
        (*it)->raise();
        (*it)->activateWindow();
        return;
    }

    QWidget* win = impl_->app.createPluginWindowFor(pluginId.toStdString(), nullptr);
    if (!win) return;

    win->setAttribute(Qt::WA_DeleteOnClose);
    win->setWindowFlag(Qt::Window);
    win->show();
    impl_->openWindows[pluginId] = win;
}

#include "PluginManagementView.moc"
