#include "ui/plugin_mgmt/PluginManagementView.h"
#include "core/app/Application.h"

#include <QHeaderView>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

struct PluginManagementView::Impl {
    clickin::Application& app;
    QTableWidget* table = nullptr;

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
    impl_->table->setColumnCount(6);
    impl_->table->setHorizontalHeaderLabels(
        {"Plugin ID", "Name", "Version", "Status", "Built-in", "Critical"});
    impl_->table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    impl_->table->setSelectionBehavior(QAbstractItemView::SelectRows);
    impl_->table->setSelectionMode(QAbstractItemView::SingleSelection);
    impl_->table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    impl_->table->verticalHeader()->hide();

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
    }
    impl_->table->resizeColumnsToContents();
    impl_->table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
}
