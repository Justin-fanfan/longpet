#include "VideoCallService.h"

#include "services/MediaSessionCoordinator.h"
#include "services/VideoCallPorts.h"

#include <QDebug>
#include <QUuid>

VideoCallService::VideoCallService(VideoCallMediaPort* mediaPort,
                                   CallPromptPlayerPort* promptPlayer,
                                   MediaSessionCoordinator* mediaSessions,
                                   QObject* parent)
    : QObject(parent),
      m_mediaPort(mediaPort),
      m_promptPlayer(promptPlayer),
      m_mediaSessions(mediaSessions)
{
    if (m_mediaPort) {
        connect(m_mediaPort, &VideoCallMediaPort::mediaReady,
                this, &VideoCallService::handleMediaReady);
        connect(m_mediaPort, &VideoCallMediaPort::failed,
                this, &VideoCallService::handleMediaFailure);
        connect(m_mediaPort, &VideoCallMediaPort::disconnected,
                this, [this](const QString& message) {
            handleMediaFailure(QStringLiteral("MEDIA_DISCONNECTED"), message);
        });
        connect(m_mediaPort, &VideoCallMediaPort::remoteVideoFrame,
                this, &VideoCallService::remoteVideoFrame);
    }
    if (m_promptPlayer) {
        connect(m_promptPlayer, &CallPromptPlayerPort::finished,
                this, &VideoCallService::continueAfterPrompt);
        connect(m_promptPlayer, &CallPromptPlayerPort::failed,
                this, [this](const QString& message) {
            qWarning().noquote() << "Call prompt failed, continuing:" << message;
            continueAfterPrompt();
        });
    }
}

VideoCallService::~VideoCallService()
{
    releaseResources();
}

VideoCallSnapshot VideoCallService::snapshot() const
{
    return m_snapshot;
}

VideoCallResult VideoCallService::startOutgoingCall(VideoCallMode mode)
{
    if (m_snapshot.isActive()) {
        return failure(VideoCallErrorCode::Busy,
                       QStringLiteral("当前已有正在进行的通话"));
    }
    if (m_mediaSessions
        && !m_mediaSessions->tryAcquire(QStringLiteral("video_call"))) {
        return failure(VideoCallErrorCode::Busy,
                       QStringLiteral("设备正在进行语音交互，请稍后再试"));
    }
    m_mediaSessionAcquired = m_mediaSessions != nullptr;

    initializeSnapshot(mode, VideoCallDirection::DeviceToFamily);
    transitionTo(VideoCallState::OutgoingRinging);
    QString error;
    if (!prepareMedia(&error)) {
        handleMediaFailure(QStringLiteral("MEDIA_INITIALIZATION_FAILED"), error);
        return failure(VideoCallErrorCode::MediaUnavailable, error);
    }
    return {true, {}, VideoCallErrorCode::None, m_snapshot};
}

VideoCallResult VideoCallService::startIncomingCall(VideoCallMode mode)
{
    if (m_snapshot.isActive()) {
        return failure(VideoCallErrorCode::Busy,
                       QStringLiteral("设备正在通话，请稍后再试"));
    }
    if (m_mediaSessions
        && !m_mediaSessions->tryAcquire(QStringLiteral("video_call"))) {
        return failure(VideoCallErrorCode::Busy,
                       QStringLiteral("设备正在进行语音交互，请稍后再试"));
    }
    m_mediaSessionAcquired = m_mediaSessions != nullptr;

    initializeSnapshot(mode, VideoCallDirection::FamilyToDevice);
    transitionTo(VideoCallState::NotifyingDevice);
    QString error;
    if (!prepareMedia(&error)) {
        handleMediaFailure(QStringLiteral("MEDIA_INITIALIZATION_FAILED"), error);
        return failure(VideoCallErrorCode::MediaUnavailable, error);
    }

    m_promptActive = false;
    if (m_promptPlayer && m_promptPlayer->play(mode, &error)) {
        m_promptActive = true;
        return {true, {}, VideoCallErrorCode::None, m_snapshot};
    }
    if (!error.isEmpty())
        qWarning().noquote() << "Call prompt unavailable, continuing:" << error;
    continueAfterPrompt();
    return {true, {}, VideoCallErrorCode::None, m_snapshot};
}

VideoCallResult VideoCallService::applyRemoteAction(
    const VideoCallActionRequest& request)
{
    if (request.callId != m_snapshot.callId || request.callId.isEmpty()) {
        return failure(VideoCallErrorCode::CallMismatch,
                       QStringLiteral("通话标识与当前通话不一致"));
    }
    if (request.expectedRevision != m_snapshot.revision) {
        return failure(VideoCallErrorCode::RevisionConflict,
                       QStringLiteral("通话状态已变化，请刷新后重试"));
    }

    switch (request.action) {
    case VideoCallAction::Accept:
        if (m_snapshot.state != VideoCallState::OutgoingRinging) {
            return failure(VideoCallErrorCode::InvalidState,
                           QStringLiteral("当前通话不能接听"));
        }
        transitionTo(VideoCallState::ConnectingMedia);
        if (m_mediaPort)
            m_mediaPort->enableAudio();
        else {
            m_snapshot.mediaReady = false;
            transitionTo(VideoCallState::Connected);
        }
        return {true, {}, VideoCallErrorCode::None, m_snapshot};
    case VideoCallAction::Reject:
        if (m_snapshot.state != VideoCallState::OutgoingRinging) {
            return failure(VideoCallErrorCode::InvalidState,
                           QStringLiteral("当前通话不能拒绝"));
        }
        return terminate(VideoCallState::Rejected);
    case VideoCallAction::HangUp:
        if (!m_snapshot.isActive()) {
            return failure(VideoCallErrorCode::InvalidState,
                           QStringLiteral("当前没有可挂断的通话"));
        }
        return terminate(VideoCallState::Ended);
    case VideoCallAction::Fail:
        if (!m_snapshot.isActive()) {
            return failure(VideoCallErrorCode::InvalidState,
                           QStringLiteral("当前没有可报告失败的通话"));
        }
        m_snapshot.errorCode = request.errorCode.isEmpty()
            ? QStringLiteral("FAMILY_MEDIA_FAILED") : request.errorCode;
        m_snapshot.errorMessage = request.errorMessage.isEmpty()
            ? QStringLiteral("家属端媒体初始化失败") : request.errorMessage;
        return terminate(VideoCallState::Failed);
    }
    return failure(VideoCallErrorCode::InvalidState,
                   QStringLiteral("不支持的通话操作"));
}

VideoCallResult VideoCallService::hangUpFromDevice()
{
    if (!m_snapshot.isActive()) {
        return failure(VideoCallErrorCode::InvalidState,
                       QStringLiteral("当前没有可挂断的通话"));
    }
    return terminate(VideoCallState::Ended);
}

void VideoCallService::initializeSnapshot(VideoCallMode mode,
                                          VideoCallDirection direction)
{
    m_snapshot = {};
    m_snapshot.callId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_snapshot.mediaToken = QUuid::createUuid().toString(QUuid::WithoutBraces)
                          + QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_snapshot.mode = mode;
    m_snapshot.direction = direction;
    m_snapshot.remoteName = QStringLiteral("家属端");
    m_snapshot.startedAt = QDateTime::currentDateTimeUtc();
    m_snapshot.mediaPort = m_mediaPort ? m_mediaPort->port() : 0;
    m_snapshot.mediaReady = false;
    m_snapshot.errorCode.clear();
    m_snapshot.errorMessage.clear();
}

bool VideoCallService::prepareMedia(QString* error)
{
    if (!m_mediaPort)
        return true;
    m_snapshot.mediaPort = m_mediaPort->port();
    const bool prepared = m_mediaPort->prepare(m_snapshot, false, error);
    if (prepared && !m_callResourcesActive) {
        m_callResourcesActive = true;
        emit callActivityChanged(true);
    }
    return prepared;
}

void VideoCallService::continueAfterPrompt()
{
    if (m_snapshot.state != VideoCallState::NotifyingDevice)
        return;
    m_promptActive = false;
    transitionTo(VideoCallState::ConnectingMedia);
    if (m_mediaPort)
        m_mediaPort->enableAudio();
    else
        transitionTo(VideoCallState::Connected);
}

void VideoCallService::handleMediaReady()
{
    if (m_snapshot.state != VideoCallState::ConnectingMedia)
        return;
    m_snapshot.mediaReady = true;
    m_snapshot.connectedAt = QDateTime::currentDateTimeUtc();
    transitionTo(VideoCallState::Connected);
}

void VideoCallService::handleMediaFailure(const QString& code,
                                          const QString& message)
{
    if (!m_snapshot.isActive())
        return;
    m_snapshot.errorCode = code;
    m_snapshot.errorMessage = message.isEmpty()
        ? QStringLiteral("媒体通道不可用") : message;
    terminate(VideoCallState::Failed);
}

void VideoCallService::releaseResources()
{
    m_promptActive = false;
    if (m_promptPlayer)
        m_promptPlayer->stop();
    if (m_mediaPort)
        m_mediaPort->stop();
    if (m_callResourcesActive) {
        m_callResourcesActive = false;
        emit callActivityChanged(false);
    }
    if (m_mediaSessionAcquired && m_mediaSessions) {
        m_mediaSessionAcquired = false;
        m_mediaSessions->release(QStringLiteral("video_call"));
    }
}

VideoCallResult VideoCallService::terminate(VideoCallState state)
{
    releaseResources();
    m_snapshot.mediaReady = false;
    return transitionTo(state);
}

VideoCallResult VideoCallService::transitionTo(VideoCallState state)
{
    m_snapshot.state = state;
    m_snapshot.updatedAt = QDateTime::currentDateTimeUtc();
    ++m_snapshot.revision;
    emit snapshotChanged(m_snapshot);
    return {true, {}, VideoCallErrorCode::None, m_snapshot};
}

VideoCallResult VideoCallService::failure(VideoCallErrorCode code,
                                          const QString& error) const
{
    return {false, error, code, m_snapshot};
}
