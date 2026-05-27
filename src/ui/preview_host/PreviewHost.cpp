#include "ui/preview_host/PreviewHost.h"
#include "core/app/Application.h"
#include "core/app/CoreContext.h"
#include "core/capability/CapabilityBroker.h"
#include "sdk/contracts/builtin/AssetRef.h"
#include "sdk/contracts/ui/AssetPreviewWidgetContract.h"

#include <QLabel>
#include <QVBoxLayout>

struct PreviewHost::Impl {
    clickin::Application& app;
    QVBoxLayout* layout      = nullptr;
    QLabel*      placeholder = nullptr;
    QWidget*     current     = nullptr;  // plugin-contributed widget

    explicit Impl(clickin::Application& a) : app(a) {}

    void swapWidget(QWidget* next) {
        if (current) {
            layout->removeWidget(current);
            current->deleteLater();
            current = nullptr;
        }
        if (next) {
            placeholder->hide();
            current = next;
            layout->addWidget(current, 1);
            current->show();
        } else {
            placeholder->show();
        }
    }
};

PreviewHost::PreviewHost(clickin::Application& app, QWidget* parent)
    : QWidget(parent)
    , impl_(std::make_unique<Impl>(app))
{
    setMinimumHeight(120);

    impl_->layout = new QVBoxLayout(this);
    impl_->layout->setContentsMargins(0, 0, 0, 0);

    impl_->placeholder = new QLabel("Select an asset and press Space or double-click to preview", this);
    impl_->placeholder->setAlignment(Qt::AlignCenter);
    impl_->placeholder->setStyleSheet("color: #888;");
    impl_->layout->addWidget(impl_->placeholder);
}

PreviewHost::~PreviewHost() = default;

void PreviewHost::onAssetSelected(const QString& assetId) {
    auto ctx = impl_->app.coreContext();
    clickin::CapabilityQuery q{};

    auto ref = ctx.capabilities.findBest<clickin::AssetPreviewWidgetContract>(q);
    if (!ref.valid()) {
        impl_->placeholder->setText("No preview available");
        impl_->swapWidget(nullptr);
        return;
    }

    auto result = ctx.capabilities
        .invoke<clickin::AssetPreviewWidgetContract>(
            ref, clickin::AssetRef{assetId.toStdString(), ""})
        .get();

    if (!result.hasPreview || !result.factory) {
        impl_->placeholder->setText("No preview available");
        impl_->swapWidget(nullptr);
        return;
    }

    impl_->placeholder->setText("Select an asset and press Space or double-click to preview");
    impl_->swapWidget(result.factory(this));
}
