#include "LocalCompanionService.h"

#include "services/MediaSessionCoordinator.h"
#include "services/OfflineVoicePorts.h"
#include "services/VoiceInteractionPorts.h"

#include <QRandomGenerator>

const QString LocalCompanionService::MediaOwner = QStringLiteral("offline_companion");

LocalCompanionService::LocalCompanionService(
    const OfflineVoiceConfiguration& configuration,
    OfflineAudioLibraryPort* library, VoiceAudioPort* audio,
    MediaSessionCoordinator* mediaSessions, QObject* parent)
    : QObject(parent), m_configuration(configuration), m_library(library),
      m_audio(audio), m_mediaSessions(mediaSessions)
{
    if (!m_audio)
        return;
    connect(m_audio, &VoiceAudioPort::playbackStarted,
            this, [this](quint64 sessionId) {
        if (m_active && sessionId == m_sessionId)
            emit playbackStarted();
    });
    connect(m_audio, &VoiceAudioPort::playbackFinished,
            this, [this](quint64 sessionId) {
        if (m_active && sessionId == m_sessionId)
            finish(true);
    });
    connect(m_audio, &VoiceAudioPort::cancellationFinished,
            this, [this](quint64 sessionId) {
        if (m_active && m_canceling && sessionId == m_sessionId)
            finish(true);
    });
    connect(m_audio, &VoiceAudioPort::audioFailed,
            this, [this](quint64 sessionId, VoiceAudioStage stage,
                         const QString& userMessage, const QString& diagnostic) {
        if (m_active && sessionId == m_sessionId
            && stage == VoiceAudioStage::Playback) {
            finish(false, userMessage, diagnostic);
        }
    });
}

bool LocalCompanionService::start(QString* error)
{
    if (error)
        error->clear();
    if (m_active) {
        if (error)
            *error = QStringLiteral("离线陪伴正在播放");
        return false;
    }
    const QString configurationError = m_configuration.validationError();
    if (!configurationError.isEmpty() || !m_configuration.enabled) {
        if (error)
            *error = configurationError.isEmpty()
                ? QStringLiteral("离线陪伴未启用") : configurationError;
        return false;
    }
    if (!m_library || !m_audio) {
        if (error)
            *error = QStringLiteral("离线陪伴服务尚未准备好");
        return false;
    }
    QString libraryError;
    QStringList clips = m_library->clipIds(&libraryError);
    if (clips.isEmpty()) {
        if (error)
            *error = libraryError.isEmpty()
                ? QStringLiteral("没有可播放的离线陪伴语音") : libraryError;
        return false;
    }
    if (clips.size() > 1 && clips.contains(m_lastClipId))
        clips.removeAll(m_lastClipId);
    const QString clipId = clips.at(
        QRandomGenerator::global()->bounded(clips.size()));
    const QByteArray audio = m_library->loadClip(clipId, &libraryError);
    if (audio.isEmpty()) {
        if (error)
            *error = libraryError.isEmpty()
                ? QStringLiteral("离线陪伴音频为空") : libraryError;
        return false;
    }
    if (m_mediaSessions && !m_mediaSessions->tryAcquire(MediaOwner)) {
        if (error)
            *error = QStringLiteral("音频设备正在使用，请稍后再试");
        return false;
    }

    m_lastClipId = clipId;
    m_sessionId = (quint64(1) << 63) | ++m_nextSession;
    m_active = true;
    m_canceling = false;
    emit activityChanged(true);
    m_audio->play(m_sessionId, audio);
    return true;
}

void LocalCompanionService::stop()
{
    if (!m_active)
        return;
    m_canceling = true;
    if (m_audio)
        m_audio->cancel(m_sessionId);
    else
        finish(true);
}

bool LocalCompanionService::isActive() const
{
    return m_active;
}

void LocalCompanionService::finish(bool success, const QString& userMessage,
                                   const QString& diagnostic)
{
    if (!m_active)
        return;
    m_active = false;
    m_canceling = false;
    m_sessionId = 0;
    if (m_mediaSessions)
        m_mediaSessions->release(MediaOwner);
    emit activityChanged(false);
    if (success)
        emit playbackFinished();
    else
        emit playbackFailed(userMessage.isEmpty()
                                ? QStringLiteral("离线语音播放失败")
                                : userMessage,
                            diagnostic);
}
