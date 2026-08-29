#pragma once

#include "model/VideoCallModels.h"

#include <QImage>
#include <QObject>

class CallPromptPlayerPort;
class VideoCallMediaPort;

class VideoCallService final : public QObject {
    Q_OBJECT

public:
    explicit VideoCallService(VideoCallMediaPort* mediaPort = nullptr,
                              CallPromptPlayerPort* promptPlayer = nullptr,
                              QObject* parent = nullptr);
    ~VideoCallService() override;

    VideoCallSnapshot snapshot() const;
    VideoCallResult startOutgoingCall(VideoCallMode mode = VideoCallMode::Video);
    VideoCallResult startIncomingCall(VideoCallMode mode);
    VideoCallResult applyRemoteAction(const VideoCallActionRequest& request);
    VideoCallResult hangUpFromDevice();

signals:
    void snapshotChanged(const VideoCallSnapshot& snapshot);
    void remoteVideoFrame(const QImage& frame);
    void localVideoFrame(const QImage& frame);
    void callActivityChanged(bool active);

private:
    void initializeSnapshot(VideoCallMode mode, VideoCallDirection direction);
    bool prepareMedia(QString* error);
    void continueAfterPrompt();
    void handleMediaReady();
    void handleMediaFailure(const QString& code, const QString& message);
    void releaseResources();
    VideoCallResult terminate(VideoCallState state);
    VideoCallResult transitionTo(VideoCallState state);
    VideoCallResult failure(VideoCallErrorCode code, const QString& error) const;

    VideoCallSnapshot m_snapshot;
    VideoCallMediaPort* m_mediaPort = nullptr;
    CallPromptPlayerPort* m_promptPlayer = nullptr;
    bool m_promptActive = false;
    bool m_callResourcesActive = false;
};
