#include "ui/preview_host/PreviewHost.h"
#include "core/app/Application.h"
#include "core/app/CoreContext.h"
#include "core/capability/CapabilityBroker.h"
#include "sdk/contracts/builtin/AssetRef.h"
#include "sdk/contracts/ui/AssetPreviewWidgetContract.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPointer>
#include <QToolButton>
#include <QVBoxLayout>

struct PreviewHost::Impl {
    clickin::Application& app;
    QVBoxLayout*  layout      = nullptr;
    QWidget*      header      = nullptr;
    QToolButton*  popOutBtn   = nullptr;
    QLabel*       placeholder = nullptr;
    QWidget*      current     = nullptr;

    QPointer<QWidget> popOutWindow;
    std::function<QWidget*(QWidget*)> windowFactory;

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
    impl_->layout->setSpacing(0);

    // Header bar — only visible when a preview with window support is active.
    impl_->header = new QWidget(this);
    auto* hLayout = new QHBoxLayout(impl_->header);
    hLayout->setContentsMargins(4, 2, 4, 2);
    hLayout->addStretch();

    impl_->popOutBtn = new QToolButton(impl_->header);
    impl_->popOutBtn->setText("⤢");   // ⤢ diagonal arrows
    impl_->popOutBtn->setToolTip("Open in window");
    hLayout->addWidget(impl_->popOutBtn);

    impl_->header->hide();
    impl_->layout->addWidget(impl_->header);

    impl_->placeholder = new QLabel(
        "Select an asset and press Space or double-click to preview", this);
    impl_->placeholder->setAlignment(Qt::AlignCenter);
    impl_->placeholder->setStyleSheet("color: #888;");
    impl_->layout->addWidget(impl_->placeholder, 1);

    connect(impl_->popOutBtn, &QToolButton::clicked,
            this, &PreviewHost::onPopOut);
}

PreviewHost::~PreviewHost() = default;

void PreviewHost::onAssetSelected(const QString& assetId) {
    auto ctx = impl_->app.coreContext();
    clickin::CapabilityQuery q{};

    impl_->windowFactory = nullptr;
    impl_->header->hide();

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

    if (!result.supportsEmbedded || !result.embeddedFactory) {
        impl_->placeholder->setText("No preview available");
        impl_->swapWidget(nullptr);
        return;
    }

    if (result.supportsWindow && result.windowFactory) {
        impl_->windowFactory = result.windowFactory;
        impl_->header->show();
    }

    impl_->placeholder->setText(
        "Select an asset and press Space or double-click to preview");
    impl_->swapWidget(result.embeddedFactory(this));
}

void PreviewHost::onPopOut() {
    if (impl_->popOutWindow) {
        impl_->popOutWindow->raise();
        impl_->popOutWindow->activateWindow();
        return;
    }
    if (!impl_->windowFactory) return;

    auto* w = new QWidget(nullptr, Qt::Window);
    w->setWindowTitle("Preview");
    w->setAttribute(Qt::WA_DeleteOnClose);
    auto* layout = new QVBoxLayout(w);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(impl_->windowFactory(w));
    w->resize(700, 220);
    w->show();
    impl_->popOutWindow = w;
}
