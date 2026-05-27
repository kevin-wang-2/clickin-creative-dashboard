#pragma once
#include <QWidget>
#include <memory>
#include <string>

namespace clickin { class CapabilityBroker; }

class AudioPreviewWidget : public QWidget {
    Q_OBJECT
public:
    explicit AudioPreviewWidget(std::string assetId,
                                 clickin::CapabilityBroker* broker,
                                 QWidget* parent = nullptr);
    ~AudioPreviewWidget();

private slots:
    void onPlayPause();
    void onStop();
    void onPositionChanged(qint64 posMs);
    void onDurationChanged(qint64 durMs);
    void onPlaybackStateChanged(int state);  // QMediaPlayer::PlaybackState as int

private:
    static QString formatTime(qint64 ms);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};
