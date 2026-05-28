#include "ui/discover/DiscoverManagerDialog.h"
#include "core/app/Application.h"
#include "core/app/CoreContext.h"
#include "sdk/contracts/builtin/DiscoverTabContract.h"

#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>

DiscoverManagerDialog::DiscoverManagerDialog(clickin::Application& app, QWidget* parent)
    : QDialog(parent)
    , app_(app)
{
    setWindowTitle("Discover Manager");
    setMinimumSize(600, 380);
    resize(760, 480);
    setWindowFlags(windowFlags() | Qt::Tool);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    tabs_ = new QTabWidget(this);
    layout->addWidget(tabs_);

    // Collect plugin-contributed discover tabs, sorted by priority.
    auto ctx  = app_.coreContext();
    auto refs = ctx.capabilities.findAll<clickin::DiscoverTabContract>();

    struct TabDesc {
        std::string label;
        int         priority;
        std::function<QWidget*(QWidget*)> factory;
    };
    std::vector<TabDesc> descs;
    for (const auto& ref : refs) {
        auto result = ctx.capabilities
            .invoke<clickin::DiscoverTabContract>(ref, clickin::DiscoverTabContract::Request{})
            .get();
        if (!result.label.empty() && result.widgetFactory)
            descs.push_back({result.label, result.priority, std::move(result.widgetFactory)});
    }

    std::sort(descs.begin(), descs.end(),
              [](const TabDesc& a, const TabDesc& b) { return a.priority < b.priority; });

    for (auto& desc : descs) {
        auto* placeholder = new QWidget(this);
        tabs_->addTab(placeholder, QString::fromStdString(desc.label));
        lazyFactories_[placeholder] = std::move(desc.factory);
    }

    connect(tabs_, &QTabWidget::currentChanged, this, &DiscoverManagerDialog::onTabChanged);

    // Eagerly create the first tab so the dialog is not blank on open.
    if (tabs_->count() > 0)
        onTabChanged(0);
}

DiscoverManagerDialog::~DiscoverManagerDialog() = default;

void DiscoverManagerDialog::onTabChanged(int index) {
    if (index < 0) return;
    QWidget* placeholder = tabs_->widget(index);
    auto it = lazyFactories_.find(placeholder);
    if (it == lazyFactories_.end()) return;

    auto factory = std::move(it->second);
    lazyFactories_.erase(it);

    QWidget* real = factory(placeholder);
    auto* inner = new QVBoxLayout(placeholder);
    inner->setContentsMargins(0, 0, 0, 0);
    inner->addWidget(real);
}
