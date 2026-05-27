#include "ui/job_status/JobStatusBar.h"
#include <QHBoxLayout>
#include <QLabel>

struct JobStatusBar::Impl {
    QLabel* label = nullptr;
};

JobStatusBar::JobStatusBar(QWidget* parent)
    : QWidget(parent)
    , impl_(std::make_unique<Impl>())
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 2, 4, 2);
    impl_->label = new QLabel("Ready", this);
    layout->addWidget(impl_->label);
    layout->addStretch();
    setMaximumHeight(28);
}

JobStatusBar::~JobStatusBar() = default;

void JobStatusBar::setStatus(const QString& text) {
    impl_->label->setText(text);
}
