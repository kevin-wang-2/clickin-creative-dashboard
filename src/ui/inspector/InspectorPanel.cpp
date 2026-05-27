#include "ui/inspector/InspectorPanel.h"
#include "core/app/Application.h"
#include "core/app/CoreContext.h"
#include "core/capability/CapabilityBroker.h"
#include "sdk/contracts/builtin/AssetRef.h"
#include "sdk/contracts/builtin/AssetNameContract.h"
#include "sdk/contracts/builtin/AssetKindContract.h"
#include "sdk/contracts/media/AudioMetadataContract.h"

#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QString>
#include <QVBoxLayout>

struct InspectorPanel::Impl {
    clickin::Application& app;

    QLabel* nameLabel       = nullptr;
    QLabel* kindLabel       = nullptr;
    QLabel* idLabel         = nullptr;

    QGroupBox* audioGroup   = nullptr;
    QLabel* durationLabel   = nullptr;
    QLabel* sampleRateLabel = nullptr;
    QLabel* channelsLabel   = nullptr;

    explicit Impl(clickin::Application& a) : app(a) {}
};

InspectorPanel::InspectorPanel(clickin::Application& app, QWidget* parent)
    : QWidget(parent)
    , impl_(std::make_unique<Impl>(app))
{
    setMinimumWidth(280);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setAlignment(Qt::AlignTop);

    auto* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight);

    impl_->nameLabel = new QLabel("—", this);
    impl_->nameLabel->setWordWrap(true);
    impl_->kindLabel = new QLabel("—", this);
    impl_->idLabel   = new QLabel("—", this);
    impl_->idLabel->setWordWrap(true);
    impl_->idLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    form->addRow("Name:",   impl_->nameLabel);
    form->addRow("Kind:",   impl_->kindLabel);
    form->addRow("ID:",     impl_->idLabel);

    impl_->audioGroup = new QGroupBox("Audio", this);
    impl_->audioGroup->hide();
    auto* audioForm = new QFormLayout(impl_->audioGroup);
    impl_->durationLabel    = new QLabel("—", impl_->audioGroup);
    impl_->sampleRateLabel  = new QLabel("—", impl_->audioGroup);
    impl_->channelsLabel    = new QLabel("—", impl_->audioGroup);
    audioForm->addRow("Duration:",    impl_->durationLabel);
    audioForm->addRow("Sample Rate:", impl_->sampleRateLabel);
    audioForm->addRow("Channels:",    impl_->channelsLabel);

    layout->addLayout(form);
    layout->addWidget(impl_->audioGroup);
    layout->addStretch();
}

InspectorPanel::~InspectorPanel() = default;

void InspectorPanel::onAssetSelected(const QString& assetId) {
    auto ctx = impl_->app.coreContext();
    clickin::AssetRef ref{assetId.toStdString(), ""};
    clickin::CapabilityQuery q{};

    auto nameRef = ctx.capabilities.findBest<clickin::AssetNameContract>(q);
    if (nameRef.valid()) {
        auto r = ctx.capabilities.invoke<clickin::AssetNameContract>(nameRef, ref).get();
        impl_->nameLabel->setText(QString::fromStdString(r.name));
    } else {
        impl_->nameLabel->setText("—");
    }

    std::string kind;
    auto kindRef = ctx.capabilities.findBest<clickin::AssetKindContract>(q);
    if (kindRef.valid()) {
        auto r = ctx.capabilities.invoke<clickin::AssetKindContract>(kindRef, ref).get();
        kind = r.kind;
        impl_->kindLabel->setText(QString::fromStdString(kind));
    } else {
        impl_->kindLabel->setText("—");
    }

    impl_->idLabel->setText(assetId);

    if (kind.rfind("audio", 0) == 0) {
        auto audioRef = ctx.capabilities.findBest<clickin::AudioMetadataContract>(q);
        if (audioRef.valid()) {
            auto m = ctx.capabilities.invoke<clickin::AudioMetadataContract>(audioRef, ref).get();
            impl_->durationLabel->setText(
                QString::number(m.durationSeconds, 'f', 2) + " s");
            impl_->sampleRateLabel->setText(
                QString::number(m.sampleRate) + " Hz");
            impl_->channelsLabel->setText(QString::number(m.channelCount));
            impl_->audioGroup->show();
            return;
        }
    }
    impl_->audioGroup->hide();
}
