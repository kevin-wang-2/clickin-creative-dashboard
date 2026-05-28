#include "providers/local_audio/AudioPreviewWidget.h"
#include "core/capability/CapabilityBroker.h"
#include "sdk/contracts/builtin/AssetRef.h"
#include "sdk/contracts/builtin/AssetLocatorContract.h"
#include "providers/audio/contracts/AudioWaveformContract.h"

#include <QAudioOutput>
#include <QHBoxLayout>
#include <QLabel>
#include <QMediaPlayer>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSizePolicy>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <vector>

// ── WaveformView ──────────────────────────────────────────────────────────────
// Defined at file scope (not in anonymous namespace) so MOC can process it.

class WaveformView : public QWidget {
    Q_OBJECT
public:
    explicit WaveformView(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumHeight(80);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    void setWaveform(std::vector<float> minVals, std::vector<float> maxVals) {
        minVals_ = std::move(minVals);
        maxVals_ = std::move(maxVals);
        hasData_ = !minVals_.empty();
        update();
    }

    void setPlayheadFraction(double f) {
        playhead_ = std::clamp(f, 0.0, 1.0);
        update();
    }

signals:
    void seekRequested(double fraction);

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), QColor(20, 20, 20));

        if (!hasData_) {
            p.setPen(QColor(130, 130, 130));
            p.drawText(rect(), Qt::AlignCenter, "Loading waveform\xe2\x80\xa6");
            return;
        }

        int   n   = static_cast<int>(maxVals_.size());
        float cy  = height() / 2.0f;
        float barW = float(width()) / n;

        for (int i = 0; i < n; ++i) {
            float x  = i * barW;
            float hi = cy - maxVals_[i] * cy;
            float lo = cy - minVals_[i] * cy;
            hi = std::clamp(hi, 0.0f, float(height()));
            lo = std::clamp(lo, 0.0f, float(height()));
            p.fillRect(QRectF(x, hi, std::max(barW - 0.5f, 1.0f), lo - hi),
                       QColor(42, 130, 218));
        }

        // Centre reference line
        p.setPen(QPen(QColor(55, 55, 55), 1));
        p.drawLine(QPointF(0, cy), QPointF(width(), cy));

        // Playhead
        if (playhead_ > 0.0) {
            p.setPen(QPen(QColor(255, 255, 255, 200), 1.5f));
            float px = float(playhead_ * width());
            p.drawLine(QPointF(px, 0), QPointF(px, height()));
        }
    }

    void mousePressEvent(QMouseEvent* ev) override {
        if (hasData_ && width() > 0) {
            double frac = double(ev->pos().x()) / width();
            emit seekRequested(std::clamp(frac, 0.0, 1.0));
        }
    }

private:
    std::vector<float> minVals_;
    std::vector<float> maxVals_;
    double playhead_ = 0.0;
    bool   hasData_  = false;
};

// ── Impl ──────────────────────────────────────────────────────────────────────

struct AudioPreviewWidget::Impl {
    std::string               assetId;
    clickin::CapabilityBroker* broker  = nullptr;

    WaveformView*  waveform     = nullptr;
    QPushButton*   playPauseBtn = nullptr;
    QPushButton*   stopBtn      = nullptr;
    QLabel*        timeLabel    = nullptr;
    QLabel*        statusLabel  = nullptr;
    QMediaPlayer*  player       = nullptr;
    QAudioOutput*  audioOutput  = nullptr;
    qint64         duration     = 0;
};

// ── AudioPreviewWidget ────────────────────────────────────────────────────────

AudioPreviewWidget::AudioPreviewWidget(std::string assetId,
                                        clickin::CapabilityBroker* broker,
                                        QWidget* parent)
    : QWidget(parent)
    , impl_(std::make_unique<Impl>())
{
    impl_->assetId = std::move(assetId);
    impl_->broker  = broker;

    // ── Layout ────────────────────────────────────────────────────────────────
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(4, 4, 4, 4);
    root->setSpacing(4);

    impl_->waveform = new WaveformView(this);
    root->addWidget(impl_->waveform, 1);

    auto* controls     = new QHBoxLayout;
    impl_->playPauseBtn = new QPushButton("\xe2\x96\xb6", this);   // ▶
    impl_->playPauseBtn->setFixedWidth(36);
    impl_->stopBtn      = new QPushButton("\xe2\x8f\xb9", this);   // ⏹
    impl_->stopBtn->setFixedWidth(36);
    impl_->timeLabel    = new QLabel("0:00 / 0:00", this);
    impl_->statusLabel  = new QLabel(this);
    impl_->statusLabel->setStyleSheet("color: #e05050;");

    controls->addWidget(impl_->playPauseBtn);
    controls->addWidget(impl_->stopBtn);
    controls->addSpacing(8);
    controls->addWidget(impl_->timeLabel);
    controls->addStretch();
    controls->addWidget(impl_->statusLabel);
    root->addLayout(controls);

    // ── Media player ──────────────────────────────────────────────────────────
    impl_->audioOutput = new QAudioOutput(this);
    impl_->audioOutput->setVolume(1.0f);
    impl_->player = new QMediaPlayer(this);
    impl_->player->setAudioOutput(impl_->audioOutput);

    connect(impl_->playPauseBtn, &QPushButton::clicked,
            this, &AudioPreviewWidget::onPlayPause);
    connect(impl_->stopBtn, &QPushButton::clicked,
            this, &AudioPreviewWidget::onStop);
    connect(impl_->player, &QMediaPlayer::positionChanged,
            this, &AudioPreviewWidget::onPositionChanged);
    connect(impl_->player, &QMediaPlayer::durationChanged,
            this, &AudioPreviewWidget::onDurationChanged);
    connect(impl_->player, &QMediaPlayer::playbackStateChanged,
            this, [this](QMediaPlayer::PlaybackState s) {
        onPlaybackStateChanged(static_cast<int>(s));
    });
    connect(impl_->player, &QMediaPlayer::errorOccurred,
            this, [this](QMediaPlayer::Error, const QString& msg) {
        impl_->statusLabel->setText(msg);
    });
    connect(impl_->waveform, &WaveformView::seekRequested,
            this, [this](double frac) {
        if (impl_->duration > 0)
            impl_->player->setPosition(static_cast<qint64>(frac * impl_->duration));
    });

    // ── Load waveform + file source ───────────────────────────────────────────
    if (!broker) return;
    clickin::AssetRef ref{impl_->assetId, ""};
    clickin::CapabilityQuery q{};

    auto wfRef = broker->findBest<clickin::AudioWaveformContract>(q);
    if (wfRef.valid()) {
        auto wf = broker->invoke<clickin::AudioWaveformContract>(wfRef, ref).get();
        if (!wf.minValues.empty())
            impl_->waveform->setWaveform(wf.minValues, wf.maxValues);
    }

    auto locRef = broker->findBest<clickin::AssetLocatorContract>(q);
    if (locRef.valid()) {
        auto loc = broker->invoke<clickin::AssetLocatorContract>(locRef, ref).get();
        if (!loc.uri.empty())
            impl_->player->setSource(QUrl(QString::fromStdString(loc.uri)));
    }
}

AudioPreviewWidget::~AudioPreviewWidget() {
    if (impl_ && impl_->player) impl_->player->stop();
}

void AudioPreviewWidget::onPlayPause() {
    if (impl_->player->playbackState() == QMediaPlayer::PlayingState)
        impl_->player->pause();
    else
        impl_->player->play();
}

void AudioPreviewWidget::onStop() {
    impl_->player->stop();
    impl_->waveform->setPlayheadFraction(0.0);
}

void AudioPreviewWidget::onPositionChanged(qint64 posMs) {
    if (impl_->duration > 0)
        impl_->waveform->setPlayheadFraction(double(posMs) / impl_->duration);
    impl_->timeLabel->setText(formatTime(posMs) + " / " + formatTime(impl_->duration));
}

void AudioPreviewWidget::onDurationChanged(qint64 durMs) {
    impl_->duration = durMs;
    impl_->timeLabel->setText("0:00 / " + formatTime(durMs));
}

void AudioPreviewWidget::onPlaybackStateChanged(int state) {
    bool playing = (state == static_cast<int>(QMediaPlayer::PlayingState));
    impl_->playPauseBtn->setText(playing ? "\xe2\x8f\xb8" : "\xe2\x96\xb6");  // ⏸ : ▶
}

QString AudioPreviewWidget::formatTime(qint64 ms) {
    qint64 s = ms / 1000;
    return QString("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QChar('0'));
}

// Must be last — triggers AUTOMOC to process this .cpp file for WaveformView.
#include "AudioPreviewWidget.moc"
