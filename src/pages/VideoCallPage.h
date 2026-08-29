#pragma once

#include "model/VideoCallModels.h"

#include <QImage>
#include <QTimer>
#include <QWidget>

class QLabel;
class QPushButton;
class PetFaceWidget;
class VideoFrameWidget;

class VideoCallPage final : public QWidget {
    Q_OBJECT

public:
    explicit VideoCallPage(QWidget* parent = nullptr);

    void setSnapshot(const VideoCallSnapshot& snapshot);
    void setRemoteVideoFrame(const QImage& frame);
    void showControlsTemporarily();

signals:
    void backRequested();
    void hangUpRequested();

private:
    void updateDuration();

    VideoCallSnapshot m_snapshot;
    QLabel* m_title = nullptr;
    QLabel* m_description = nullptr;
    QLabel* m_remoteName = nullptr;
    QLabel* m_callId = nullptr;
    QLabel* m_duration = nullptr;
    VideoFrameWidget* m_remoteVideo = nullptr;
    QWidget* m_audioStage = nullptr;
    QWidget* m_controlOverlay = nullptr;
    PetFaceWidget* m_speakingFace = nullptr;
    QPushButton* m_backButton = nullptr;
    QPushButton* m_hangUpButton = nullptr;
    QTimer m_controlsTimer;
    QTimer m_durationTimer;
};
